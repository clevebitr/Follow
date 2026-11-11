/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

/* 
 * 分工：
 *   main.c     生命周期、键盘、定时器
 *   player.c   播放控制、网络解析、曲目信息
 *   lyricview  歌词加载、在线源、偏移、当前行
 *   cover.c    封面加载（本地 / 网络缓存）与绘制
 *   backdrop   动态背景（模糊封面 + 漂移的种子色；GPU 失败时的 CPU 回退）
 *   backdropgl 动态背景 GPU 层（光斑 / 粒子 / 压暗 shader）
 *   wallgl     歌曲墙 GPU 层（磁贴纹理 + 相机 / 选中 / 淡入）
 *   glutil     墙 / 背景共用的 shader、纹理上传、HiDPI 工具
 *   qrcard     二维码槽、平台胶囊
 *   layout     一次绘制用到的所有几何量
 *   stage      主画面（大时钟 / 备注 / 歌词 / 右列 / 统计）
 *   help       F1 快捷键页
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "backdrop.h"
#include "backdropgl.h"
#include "cache.h"
#include "config.h"
#include "cover.h"
#include "http.h"
#include "keymap.h"
#include "lyricview.h"
#include "media.h"
#include "player.h"
#include "playlist.h"
#include "qrcard.h"
#include "resolvecache.h"
#include "stage.h"
#include "stats.h"
#include "ui.h"
#include "viz.h"
#include "wallgl.h"

#ifndef FOLLOW_APP_ID
#define FOLLOW_APP_ID "org.github.clevebitr.follow"
#endif
#ifndef FOLLOW_APP_FLAGS
#define FOLLOW_APP_FLAGS G_APPLICATION_DEFAULT_FLAGS
#endif

#ifndef CSS_PATH
#define CSS_PATH "src/style/style.css"
#endif
#ifndef CONFIG_PATH
#define CONFIG_PATH "src/config.ini"
#endif
#define TICK_MS     100

App app;

static gboolean on_tick(gpointer data);
static gboolean on_anim_tick(gpointer data);

/* clock 100ms */
static void track_listening_time(gboolean playing)
{
    gint64 now_us;

    if (!playing) {
        app.stats_last_us = 0;
        return;
    }

    now_us = g_get_real_time();

    if (app.stats_last_us > 0) {
        double dt = (double)(now_us - app.stats_last_us) / 1000000.0;

        if (dt > 0.0 && dt < 5.0)
            app.stats_fraction += dt;

        if (app.stats_fraction >= 1.0) {
            guint whole = (guint)app.stats_fraction;

            stats_add(&app.stats, (double)whole);
            app.stats_fraction -= (double)whole;
        }
    }

    app.stats_last_us = now_us;

    if (now_us - app.stats_saved_us > 30 * G_USEC_PER_SEC) {
        stats_save(&app.stats);
        app.stats_saved_us = now_us;
    }
}

static gboolean on_anim_tick(gpointer data)
{
    gboolean stage_anim  = fx_running(&app.fx_stage);
    gboolean chrome_anim = playlist_chrome_animating();
    gboolean wall_open   = playlist_is_open();
    gboolean wall_gpu    = wall_open && wallgl_ready();
    gboolean wall_cpu    = wall_open && !wall_gpu;
    gboolean pending     = playlist_frame_pending();
    gboolean busy        = stage_anim || chrome_anim;

    (void)data;

    /* GL 墙由 wallgl 的 frame clock tick 在 vsync 上重建；这个 16ms 泵
     * 只在 pending 时 queue_draw，让 tick 持续跑。墙控件在开 / 关时会被
     * 隐藏，tick 会通过 map / unmap 信号重新挂上。 */
    if (wall_gpu && pending && app.wallgl != NULL)
        gtk_widget_queue_draw(app.wallgl);

    if (app.canvas != NULL && gtk_widget_get_visible(app.canvas) &&
        (stage_anim || wall_cpu))
        gtk_widget_queue_draw(app.canvas);

    if (app.chrome != NULL && gtk_widget_get_visible(app.chrome) && chrome_anim)
        gtk_widget_queue_draw(app.chrome);

    if (wall_cpu)
        busy = busy || playlist_animating();
    if (wall_gpu)
        busy = busy || pending || playlist_animating();

    if (!busy) {
        app.anim_id = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

void ui_animate(void)
{
    if (app.anim_id == 0)
        app.anim_id = g_timeout_add(16, on_anim_tick, NULL);
}

void ui_sync_layers(void)
{
    static gint64   warm_start;
    static gboolean warm_done;
    gint64           now = g_get_monotonic_time();
    gboolean         wall_open = playlist_is_open();
    gboolean         wall_full = wall_open && !playlist_is_closing();
    gboolean         gpu_wall  = wall_full && wallgl_ready();
    gboolean         gpu_bg    = backdropgl_ready();
    gboolean         show_wallgl;

    if (warm_start == 0)
        warm_start = now;

    if (wallgl_ready())
        warm_done = TRUE;

    /* 关墙后把 GLArea 真正隐藏：在 DComp 下 GtkGLArea 是原生子表面，
     * 单纯把 opacity 设成 0 不一定能让它从画面上消失。第一次启动时
     * 先留着它预热，2 秒内还没 ready（没有 GPU）就不再等。 */
    show_wallgl = wall_open ||
                  (!warm_done && now - warm_start < 2 * G_USEC_PER_SEC);

    if (app.wallgl != NULL)
        gtk_widget_set_visible(app.wallgl, show_wallgl);

    if (app.bg_gl != NULL)
        gtk_widget_set_visible(app.bg_gl, !wall_full && gpu_bg);

    if (app.bg_picture != NULL)
        gtk_widget_set_visible(app.bg_picture, !wall_full && !gpu_bg);

    /* 墙全屏时主画布完全被盖住；退出动画期间要留着它垫在主界面下面。 */
    if (app.canvas != NULL)
        gtk_widget_set_visible(app.canvas, !gpu_wall);

    /* GL 墙的表头 / 底栏在 chrome 层；cairo 回退时 chrome 由画布一起画。 */
    if (app.chrome != NULL)
        gtk_widget_set_visible(app.chrome, wall_open && wallgl_ready());
}

static gboolean on_tick(gpointer data)
{
    gboolean playing, paused;
    double   position;
    int      second, width, height;

    (void)data;

    player_check_finished();
    player_check_load_failure();

    ui_sync_layers();

    playing = media_is_playing(&app.media);
    paused  = media_is_paused(&app.media);

    track_listening_time(playing);

    width  = gtk_widget_get_width(app.window);
    height = gtk_widget_get_height(app.window);

    if (width != app.last_width || height != app.last_height) {
        app.last_width  = width;
        app.last_height = height;
        ui_request_redraw();

        if (playlist_is_open())
            playlist_request_frame();
    }

    position = media_position(&app.media);
    second   = (int)position;

    if (second != app.shown_second) {
        app.shown_second = second;
        ui_request_redraw();

        if (playlist_is_open()) {
            app.chrome_dirty = TRUE;
            playlist_request_frame();
        }
    }

    if (playing != app.last_playing || paused != app.last_paused) {
        app.last_playing = playing;
        app.last_paused  = paused;
        ui_request_redraw();
    }

    {
        gint64 wall_second = g_get_real_time() / G_USEC_PER_SEC;

        if (wall_second != app.shown_wall_second) {
            app.shown_wall_second = wall_second;
            ui_request_redraw();
        }
    }

    lyricview_sync(position);

    if (app.dirty) {
        app.dirty = FALSE;

        if (app.canvas != NULL && gtk_widget_get_visible(app.canvas))
            gtk_widget_queue_draw(app.canvas);
    }

    if (app.chrome_dirty) {
        app.chrome_dirty = FALSE;

        if (app.chrome != NULL && gtk_widget_get_visible(app.chrome))
            gtk_widget_queue_draw(app.chrome);
    }

    return G_SOURCE_CONTINUE;
}

/* keyboard */
static void toggle_fullscreen(void)
{
    GtkWindow *window = GTK_WINDOW(app.window);

    if (gtk_window_is_fullscreen(window))
        gtk_window_unfullscreen(window);
    else
        gtk_window_fullscreen(window);
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state, gpointer data)
{
    const gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    const double   seek  = shift ? 30.0 : 5.0;

    (void)controller;
    (void)keycode;
    (void)data;

    /* 快捷键页：只认帮助 / 返回 / 退出，其他键一律吞掉 */
    if (app.help) {
        if (keymap_match(KEY_HELP, keyval, state) ||
            keymap_match(KEY_BACK, keyval, state)) {
            app.help = FALSE;
            app.chrome_dirty = TRUE;
            ui_request_redraw();
        } else if (keymap_match(KEY_QUIT, keyval, state)) {
            gtk_window_close(GTK_WINDOW(app.window));
        } else if (keymap_match(KEY_PLAYLIST, keyval, state)) {
            app.help = FALSE;
            app.chrome_dirty = TRUE;
            playlist_open_page();
        }

        return TRUE;
    }

    /* 播放列表页：方向键选磁贴、回车播放、Tab / Esc 关闭 */
    if (playlist_is_open()) {
        if (keymap_match(KEY_PLAYLIST, keyval, state) ||
            keymap_match(KEY_BACK, keyval, state)) {
            playlist_close();
            return TRUE;
        }

        if (keymap_match(KEY_HELP, keyval, state)) {
            playlist_close();
            app.help = TRUE;
            app.chrome_dirty = TRUE;
            ui_request_redraw();
            return TRUE;
        }

        if (keymap_match(KEY_QUIT, keyval, state)) {
            gtk_window_close(GTK_WINDOW(app.window));
            return TRUE;
        }

        switch (keyval) {
        case GDK_KEY_Left:
            playlist_move(-1, 0);
            return TRUE;
        case GDK_KEY_Right:
            playlist_move(+1, 0);
            return TRUE;
        case GDK_KEY_Up:
            playlist_move(0, -1);
            return TRUE;
        case GDK_KEY_Down:
            playlist_move(0, +1);
            return TRUE;
        case GDK_KEY_Page_Up:
            playlist_page(-1);
            return TRUE;
        case GDK_KEY_Page_Down:
            playlist_page(+1);
            return TRUE;
        case GDK_KEY_Home:
            playlist_home();
            return TRUE;
        case GDK_KEY_End:
            playlist_end();
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            playlist_activate();
            return TRUE;
        default:
            break;
        }

        if (keymap_match(KEY_PLAY_PAUSE, keyval, state)) {
            playlist_activate();
            return TRUE;
        }

        return TRUE;
    }

    if (keymap_match(KEY_PLAY_PAUSE, keyval, state)) {
        player_toggle();
        return TRUE;
    }
    if (keymap_match(KEY_SEEK_BACK, keyval, state)) {
        player_seek_by(-seek);
        return TRUE;
    }
    if (keymap_match(KEY_SEEK_FORWARD, keyval, state)) {
        player_seek_by(+seek);
        return TRUE;
    }
    if (keymap_match(KEY_NEXT, keyval, state)) {
        player_play_neighbour(+1);
        return TRUE;
    }
    if (keymap_match(KEY_PREV, keyval, state)) {
        player_play_neighbour(-1);
        return TRUE;
    }
    if (keymap_match(KEY_RESTART, keyval, state)) {
        player_restart();
        return TRUE;
    }
    if (keymap_match(KEY_STOP, keyval, state)) {
        player_stop();
        return TRUE;
    }
    if (keymap_match(KEY_LYRIC_EARLIER, keyval, state)) {
        lyricview_shift_offset(-250);
        return TRUE;
    }
    if (keymap_match(KEY_LYRIC_LATER, keyval, state)) {
        lyricview_shift_offset(+250);
        return TRUE;
    }
    if (keymap_match(KEY_LYRIC_RESET, keyval, state)) {
        lyricview_reset_offset();
        return TRUE;
    }
    if (keymap_match(KEY_REPEAT, keyval, state)) {
        player_cycle_repeat();
        return TRUE;
    }
    if (keymap_match(KEY_SHUFFLE, keyval, state)) {
        player_toggle_shuffle();
        return TRUE;
    }
    if (keymap_match(KEY_FULLSCREEN, keyval, state)) {
        toggle_fullscreen();
        return TRUE;
    }
    if (keymap_match(KEY_PLAYLIST, keyval, state)) {
        playlist_open_page();
        return TRUE;
    }
    if (keymap_match(KEY_HELP, keyval, state)) {
        app.help = TRUE;
        app.chrome_dirty = TRUE;
        ui_request_redraw();
        return TRUE;
    }
    if (keymap_match(KEY_QUIT, keyval, state)) {
        gtk_window_close(GTK_WINDOW(app.window));
        return TRUE;
    }
    if (keymap_match(KEY_BACK, keyval, state)) {
        if (gtk_window_is_fullscreen(GTK_WINDOW(app.window)))
            gtk_window_unfullscreen(GTK_WINDOW(app.window));
        return TRUE;
    }

    return FALSE;
}

/* 鼠标：歌曲墙的拖动 / 滚轮 / 悬停 / 点击 */
static void on_canvas_released(GtkGestureClick *gesture, int n_press,
                               double x, double y, gpointer data)
{
    (void)gesture;
    (void)n_press;
    (void)data;

    if (!playlist_was_dragging())
        playlist_click(x, y);
}

static void on_canvas_drag_begin(GtkGestureDrag *gesture, double start_x,
                                  double start_y, gpointer data)
{
    (void)gesture;
    (void)data;

    playlist_pan_begin(start_x, start_y);
}

static void on_canvas_drag_update(GtkGestureDrag *gesture, double offset_x,
                                   double offset_y, gpointer data)
{
    (void)gesture;
    (void)data;

    playlist_pan_update(offset_x, offset_y);
}

static void on_canvas_drag_end(GtkGestureDrag *gesture, double offset_x,
                                double offset_y, gpointer data)
{
    (void)gesture;
    (void)data;

    playlist_pan_end(offset_x, offset_y);
}

static void on_canvas_motion(GtkEventControllerMotion *controller,
                             double x, double y, gpointer data)
{
    (void)controller;
    (void)data;

    playlist_hover(x, y);
}

static gboolean on_canvas_scroll(GtkEventControllerScroll *controller,
                                 double dx, double dy, gpointer data)
{
    (void)controller;
    (void)data;

    if (!playlist_is_open())
        return FALSE;

    playlist_pan_by(dx, dy);
    return TRUE;
}


/* 墙的鼠标事件：GL 墙打开时 canvas 会被隐藏、chrome 是最上面的接收层；
 * cairo 回退时事件仍然落在 canvas 上。两个控件各自挂一套控制器。 */
static void attach_wall_input(GtkWidget *widget)
{
    GtkGesture         *click  = gtk_gesture_click_new();
    GtkGesture         *drag   = gtk_gesture_drag_new();
    GtkEventController *motion = gtk_event_controller_motion_new();
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);

    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);

    g_signal_connect(click, "released", G_CALLBACK(on_canvas_released), NULL);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_canvas_drag_begin), NULL);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_canvas_drag_update), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_canvas_drag_end), NULL);
    g_signal_connect(motion, "motion", G_CALLBACK(on_canvas_motion), NULL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_canvas_scroll), NULL);

    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag));
    gtk_widget_add_controller(widget, motion);
    gtk_widget_add_controller(widget, scroll);
}

/* 窗口销毁时先把定时器和控件指针收干净：GApplication 的 shutdown
 * 比窗口销毁晚一步，晚到的 tick 不能再碰已经释放的 canvas。 */
static void on_window_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    if (app.tick_id != 0) {
        g_source_remove(app.tick_id);
        app.tick_id = 0;
    }
    if (app.bg_tick_id != 0) {
        g_source_remove(app.bg_tick_id);
        app.bg_tick_id = 0;
    }
    if (app.anim_id != 0) {
        g_source_remove(app.anim_id);
        app.anim_id = 0;
    }

    app.canvas     = NULL;
    app.chrome     = NULL;
    app.wallgl     = NULL;
    app.bg_picture = NULL;
    app.bg_gl      = NULL;
}

static gboolean debug_open_wall(gpointer data);

static gboolean debug_close_wall(gpointer data)
{
    (void)data;

    g_message("debug: closing wall (FOLLOW_WALL_AUTO_CLOSE)");
    playlist_close();

    if (g_getenv("FOLLOW_WALL_AUTO_REOPEN") != NULL)
        g_timeout_add(1200, debug_open_wall, NULL);

    return G_SOURCE_REMOVE;
}

static gboolean debug_open_wall(gpointer data)
{
    static gboolean auto_closed;

    (void)data;

    g_message("debug: opening wall (FOLLOW_WALL_AUTO)");
    playlist_open_page();
    g_message("debug: wall open=%d closing=%d ready=%d vis=%.2f",
              playlist_is_open(), playlist_is_closing(), wallgl_ready(), 0.0);

    if (g_getenv("FOLLOW_WALL_AUTO_CLOSE") != NULL && !auto_closed) {
        auto_closed = TRUE;
        g_timeout_add(2000, debug_close_wall, NULL);
    }

    return G_SOURCE_REMOVE;
}

static gboolean debug_quit(gpointer data)
{
    (void)data;

    g_message("debug: quitting (FOLLOW_EXIT_MS)");

    if (app.window != NULL)
        gtk_window_close(GTK_WINDOW(app.window));

    return G_SOURCE_REMOVE;
}

static gboolean debug_pan(gpointer data)
{
    static int ticks;

    (void)data;

    if (ticks++ > 1200)
        return G_SOURCE_REMOVE;

    if (playlist_is_open())
        playlist_pan_by(0.55, 0.20);

    return G_SOURCE_CONTINUE;
}

static gboolean debug_pan_start(gpointer data)
{
    (void)data;

    g_timeout_add(8, debug_pan, NULL);
    return G_SOURCE_REMOVE;
}

static void activate(GtkApplication *app_, gpointer user_data)
{
    const APPconfig    *cfg = config_get();
    GtkWidget          *window;
    GtkWidget          *header_bar;
    GtkWidget          *overlay;
    GtkEventController *keys;

    (void)app_;
    (void)user_data;

    window  = gtk_application_window_new(GTK_APPLICATION(app_));
    app.window = window;

    header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
    gtk_window_set_title(GTK_WINDOW(window), cfg->title);
    gtk_window_set_default_size(GTK_WINDOW(window), cfg->width, cfg->height);

    if (cfg->fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(window));

    /* background */
    app.bg_picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(app.bg_picture), GTK_CONTENT_FIT_FILL);
    gtk_picture_set_can_shrink(GTK_PICTURE(app.bg_picture), TRUE);
    gtk_widget_set_hexpand(app.bg_picture, TRUE);
    gtk_widget_set_vexpand(app.bg_picture, TRUE);
    gtk_widget_add_css_class(app.bg_picture, "bg-picture");

    /* pic */
    app.canvas = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app.canvas, TRUE);
    gtk_widget_set_vexpand(app.canvas, TRUE);
    gtk_widget_add_css_class(app.canvas, "canvas");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.canvas), stage_render, NULL, NULL);

    /* 歌曲墙：GL 层画磁贴，透明 chrome 层画表头 / 底栏。
     * GL 墙打开时 canvas 被隐藏，chrome 是顶层接收层；cairo 回退时
     * 事件仍然落在 canvas 上。 */
    app.wallgl = wallgl_new();

    app.chrome = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app.chrome, TRUE);
    gtk_widget_set_vexpand(app.chrome, TRUE);
    gtk_widget_set_can_target(app.chrome, TRUE);
    gtk_widget_add_css_class(app.chrome, "canvas");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.chrome),
                                   stage_chrome_render, NULL, NULL);

    /* 动态背景：优先 GL；上下文 / shader 失败时保持 GtkPicture 的 CPU 路径。 */
    app.bg_gl = backdropgl_new();

    overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), app.bg_picture);

    if (app.bg_gl != NULL)
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.bg_gl);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.canvas);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.wallgl);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.chrome);
    gtk_window_set_child(GTK_WINDOW(window), overlay);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
    gtk_widget_add_controller(window, keys);

    attach_wall_input(app.canvas);
    attach_wall_input(app.chrome);

    app.track        = -1;
    app.shown_second = -1;
    app.repeat       = cfg->repeat;
    app.shuffle      = cfg->shuffle;

    /* 启动时先把第 0 首的资料摆上去，按下 Space 才开始播放 */
    cover_set(config_track_at(cfg, 0));
    qrcard_update(config_track_at(cfg, 0));
    player_update_window_title();

    gtk_window_present(GTK_WINDOW(window));

    app.tick_id = g_timeout_add(TICK_MS, on_tick, NULL);

    backdrop_push(cfg->width, cfg->height);
    app.bg_tick_id = g_timeout_add(33, backdrop_tick, NULL);

    /* 开发 / 性能测试用：自动开墙、定时退出。 */
    if (g_getenv("FOLLOW_WALL_AUTO") != NULL)
        g_timeout_add(1200, debug_open_wall, NULL);

    if (g_getenv("FOLLOW_WALL_AUTO_PAN") != NULL)
        g_timeout_add(1600, debug_pan_start, NULL);

    if (g_getenv("FOLLOW_EXIT_MS") != NULL)
        g_timeout_add((guint)MAX(100, atoi(g_getenv("FOLLOW_EXIT_MS"))),
                      debug_quit, NULL);
}

static void startup(GtkApplication *app_, gpointer user_data)
{
    GtkCssProvider *provider;

    (void)app_;
    (void)user_data;

    http_global_init();

    if (!g_file_test(CSS_PATH, G_FILE_TEST_EXISTS))
        g_warning("CSS file is not found in: %s", CSS_PATH);

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, CSS_PATH);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

#ifndef FOLLOW_SKIP_AUDIO
    if (media_init(&app.media) != 0)
        g_warning("media engine init failed");
#endif

    if (!g_file_test(CONFIG_PATH, G_FILE_TEST_EXISTS)) {
        g_warning("config file is not found in: %s (using defaults)", CONFIG_PATH);
    } else {
        g_message("config: loading %s (cwd = %s)", CONFIG_PATH, g_get_current_dir());
    }

    config_load(CONFIG_PATH);

    cache_prune_prefix("cover", config_cover_max_bytes());
    resolvecache_prune();

    g_message("cache: cover %.1f MB / limit %.0f MB",
              (double)cache_prefix_bytes("cover") / 1048576.0,
              (double)config_cover_max_bytes() / 1048576.0);

    keymap_init();
    viz_configure(config_viz_effect(), config_viz_particles(), config_viz_gain());

    g_message("visualizer: %s, %d particle(s), gain %.2f",
              viz_effect_name(), config_viz_particles(), config_viz_gain());
    g_message("keys: play=%s  next=%s  playlist=%s  help=%s",
              keymap_label(KEY_PLAY_PAUSE), keymap_label(KEY_NEXT),
              keymap_label(KEY_PLAYLIST), keymap_label(KEY_HELP));

    /* [net] proxy 同时也交给 FFmpeg：curl 走 CURLOPT_PROXY，
     * 解码器（avformat 的 http）只认环境变量，两边都指到同一个代理。 */
    if (config_proxy() != NULL) {
        g_setenv("http_proxy",  config_proxy(), TRUE);
        g_setenv("https_proxy", config_proxy(), TRUE);
        g_setenv("HTTP_PROXY",  config_proxy(), TRUE);
        g_setenv("HTTPS_PROXY", config_proxy(), TRUE);
        g_message("net: proxy = %s", config_proxy());
    }

    stats_init(&app.stats);
    stats_load(&app.stats);

    {
        const APPconfig *cfg = config_get();

        g_message("config: %" G_GSIZE_FORMAT " track(s), window %dx%d%s",
                  cfg->n_tracks, cfg->width, cfg->height,
                  cfg->fullscreen ? " (fullscreen)" : "");

        for (gsize i = 0; i < cfg->n_tracks; i++) {
            const AudioTrack *t = config_track_at(cfg, i);
            g_autofree gchar *label = config_track_label(t);

            g_message("track[%" G_GSIZE_FORMAT "] %s  %s  link=%s%s  page=%s  lrc=%s%s%dms",
                      i, t->id, label != NULL ? label : "-",
                      t->link, t->link_remote ? " (remote)" : "",
                      t->page != NULL ? t->page : "-",
                      t->lrc != NULL ? t->lrc : "-",
                      t->lrc_offset_ms != 0 ? "  offset=" : "  offset=+",
                      t->lrc_offset_ms);
        }

        g_message("config: custom qr = %s",
                  (cfg->qrcode_link != NULL && config_is_remote(cfg->qrcode_link))
                      ? cfg->qrcode_link : "(local/none, hidden)");
    }
}

static void shutdown_app(GtkApplication *app_, gpointer user_data)
{
    (void)app_;
    (void)user_data;

    if (app.tick_id != 0)
        g_source_remove(app.tick_id);
    if (app.bg_tick_id != 0)
        g_source_remove(app.bg_tick_id);
    if (app.anim_id != 0) {
        g_source_remove(app.anim_id);
        app.anim_id = 0;
    }

    g_clear_pointer(&app.snapshot, cairo_surface_destroy);

    stats_save(&app.stats);
    stats_clear(&app.stats);

    media_clear(&app.media);
    player_clear();
    lyricview_clear();
    cover_clear();
    backdrop_drop();
    backdropgl_clear();
    qrcard_clear();
    playlist_clear();
    keymap_clear();
    config_free();

    http_global_cleanup();
}

int main(int argc, char **argv)
{
    GtkApplication *app_;
    int             status;

#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);

    /* GTK 4.24 的 Win32 后端默认不创建 DirectComposition 设备，GSK 于是
     * 一直退回 cairo 软件合成，2K 全屏墙每帧都要把 GL 纹理读回内存。
     * 打开 dcomp 后 GSK 才能用 GL 渲染器；驱动不支持时 GTK 会自己退回
     * cairo。想强制关掉可以设 FOLLOW_DCOMP=0 或 GSK_RENDERER=cairo。 */
    if (g_strcmp0(g_getenv("FOLLOW_DCOMP"), "0") != 0 &&
        g_strcmp0(g_getenv("GSK_RENDERER"), "cairo") != 0) {
        const gchar *cur = g_getenv("GDK_DEBUG");

        if (cur == NULL || strstr(cur, "dcomp") == NULL) {
            gchar *next = (cur != NULL && *cur != '\0')
                ? g_strconcat(cur, ",dcomp", NULL)
                : g_strdup("dcomp");

            g_setenv("GDK_DEBUG", next, TRUE);
            g_message("win32: GDK_DEBUG=%s (DirectComposition)", next);
            g_free(next);
        }
    }
#endif

    app_ = gtk_application_new(FOLLOW_APP_ID, FOLLOW_APP_FLAGS);
    if (app_ == NULL)
        return 1;

    g_signal_connect(app_, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app_, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app_, "shutdown", G_CALLBACK(shutdown_app), NULL);

    status = g_application_run(G_APPLICATION(app_), argc, argv);

    g_object_unref(app_);
    return status;
}
