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
#include <dwmapi.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#if defined(_WIN32)
#include <gdk/win32/gdkwin32.h>
#endif

#include "backdrop.h"
#include "backdropgl.h"
#include "cache.h"
#include "config.h"
#include "cover.h"
#include "http.h"
#include "keymap.h"
#include "lyricgl.h"
#include "lyricview.h"
#include "media.h"
#include "cmdbox.h"
#include "player.h"
#include "playlist.h"
#include "qrcard.h"
#include "resolvecache.h"
#include "settings.h"
#include "stage.h"
#include "stats.h"
#include "ui.h"
#include "uidbg.h"
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
#if defined(_WIN32)
static void apply_window_edge(GtkWidget *window)
{
    GdkSurface *surface;
    HWND        hwnd;

    if (window == NULL)
        return;

    surface = gtk_native_get_surface(gtk_widget_get_native(window));
    if (surface == NULL)
        return;

    hwnd = (HWND)gdk_win32_surface_get_handle(surface);
    if (hwnd == NULL)
        return;

    {
        COLORREF border = DWMWA_COLOR_NONE;
        DWORD    corner = DWMWCP_DONOTROUND;
        DWORD    nc     = DWMNCRP_DISABLED;

        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));
        /* 透明阴影区在 DComp 下会被画成黑边，直接关掉非客户区渲染。 */
        DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY,
                              &nc, sizeof(nc));
    }
}

static gboolean apply_window_edge_idle(gpointer data)
{
    apply_window_edge(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}

static void on_window_realize(GtkWidget *widget, gpointer data)
{
    (void)data;

    apply_window_edge(widget);
}
#endif

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
    /* 歌词换行过渡：画布不可见时不算活跃（否则会空转） */
    gboolean lyric_anim  = fx_running(&app.fx_lyric) && app.canvas != NULL &&
                           gtk_widget_get_visible(app.canvas);
    gboolean busy        = stage_anim || chrome_anim || lyric_anim;

    (void)data;

    /* GL 墙由 wallgl 的 frame clock tick 在 vsync 上重建；这个 16ms 泵
     * 只在 pending 时 queue_draw，让 tick 持续跑。墙控件在开 / 关时会被
     * 隐藏，tick 会通过 map / unmap 信号重新挂上。 */
    if (wall_gpu && pending && app.wallgl != NULL)
        gtk_widget_queue_draw(app.wallgl);

    if (app.canvas != NULL && gtk_widget_get_visible(app.canvas) &&
        (stage_anim || lyric_anim || wall_cpu))
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

    /* 歌词 GPU 层跟着主画布：墙打开 / 关闭、帮助页都要同步 */
    if (app.lyricgl != NULL)
        gtk_widget_set_visible(app.lyricgl,
                               !wall_open && !app.help && !gpu_wall);

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
void ui_toggle_fullscreen(void)
{
    GtkWindow *window = GTK_WINDOW(app.window);

    if (gtk_window_is_fullscreen(window))
        gtk_window_unfullscreen(window);
    else
        gtk_window_fullscreen(window);
}

void ui_reload_config(void)
{
    const APPconfig  *cfg;
    const AudioTrack *old_track = NULL;
    g_autofree gchar *old_pic   = NULL;
    gint              track     = app.track;

    if (track >= 0 && config_get()->n_tracks > 0)
        old_track = config_track_at(config_get(), track);

    if (old_track != NULL)
        old_pic = g_strdup(old_track->pic);

    cfg = config_load(CONFIG_PATH);

    keymap_init();
    viz_configure(config_viz_effect(), config_viz_particles(), config_viz_gain());

    app.repeat  = cfg->repeat;
    app.shuffle = cfg->shuffle;

    playlist_reload();

    if (track >= 0 && (gsize)track < cfg->n_tracks) {
        const AudioTrack *now = config_track_at(cfg, (gsize)track);

        if (now != NULL) {
            if (g_strcmp0(old_pic, now->pic) != 0)
                cover_set(now);

            qrcard_update(now);
        }
    } else if (track >= 0) {
        player_stop();
        app.track = -1;

        if (cfg->n_tracks > 0) {
            const AudioTrack *first = config_track_at(cfg, 0);

            cover_set(first);
            qrcard_update(first);
        } else {
            cover_set(NULL);
            qrcard_update(NULL);
        }
    } else if (cfg->n_tracks > 0) {
        qrcard_update(config_track_at(cfg, 0));
    }

    player_update_window_title();

    app.chrome_dirty = TRUE;
    ui_request_redraw();
    ui_animate();

    g_message("config: reloaded %s (%" G_GSIZE_FORMAT " tracks)",
              CONFIG_PATH, cfg->n_tracks);
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state, gpointer data)
{
    const gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    const double   seek  = shift ? 30.0 : 5.0;

    (void)controller;
    (void)keycode;
    (void)data;

    {
        const gboolean ctrl_shift_p =
            (state & GDK_CONTROL_MASK) != 0 &&
            (state & GDK_SHIFT_MASK) != 0 &&
            (keyval == GDK_KEY_P || keyval == GDK_KEY_p);

        if (palette_is_open()) {
            if (ctrl_shift_p) {
                palette_close();
                return TRUE;
            }

            if (keyval == GDK_KEY_Escape) {
                palette_close();
                return TRUE;
            }

            /* 其他按键留给输入框，不要触发全局快捷键。 */
            return FALSE;
        }

        if (ctrl_shift_p) {
            palette_open();
            return TRUE;
        }
    }

    /* 快捷键页：只认帮助 / 返回 / 退出，其他键一律吞掉 */
    if (app.help) {
        if (keymap_match(KEY_HELP, keyval, state) ||
            keymap_match(KEY_BACK, keyval, state)) {
            app.help = FALSE;
            app.chrome_dirty = TRUE;
            ui_request_redraw();
            ui_sync_layers();
        } else if (keymap_match(KEY_QUIT, keyval, state)) {
            gtk_window_close(GTK_WINDOW(app.window));
        } else if (keymap_match(KEY_PLAYLIST, keyval, state)) {
            app.help = FALSE;
            app.chrome_dirty = TRUE;
            playlist_open_page();
            ui_sync_layers();
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
            ui_sync_layers();
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
        ui_toggle_fullscreen();
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
        ui_sync_layers();
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

/* 鼠标：歌曲墙的拖动 / 滚轮 / 悬停 / 点击 */static void on_canvas_released(GtkGestureClick *gesture, int n_press,
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

static void on_window_clicked(GtkGestureClick *gesture, int n_press,
                              double x, double y, gpointer data)
{
    (void)gesture;
    (void)n_press;
    (void)data;

    if (palette_is_open() && !palette_contains(x, y)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        palette_close();
    }
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

/* ---------- agent 调试通道的输入注入（uidbg） ---------- */

/* 命令面板是真实 GTK 控件：它开着的时候 Enter/↑/↓/Esc 走面板自己的
 * 选中逻辑（palette_debug_steps），其他键交给全局快捷键。 */
gboolean uidbg_hook_key(guint keyval, GdkModifierType state)
{
    if (palette_is_open()) {
        switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            palette_debug_steps("enter");
            return TRUE;
        case GDK_KEY_Down:
            palette_debug_steps("down");
            return TRUE;
        case GDK_KEY_Up:
            palette_debug_steps("up");
            return TRUE;
        case GDK_KEY_Escape:
            palette_debug_steps("esc");
            return TRUE;
        default:
            break;
        }
    }

    return on_key_pressed(NULL, keyval, 0, state, NULL);
}

void uidbg_hook_click(double x, double y, gboolean double_click)
{
    (void)double_click;

    if (palette_is_open() && !palette_contains(x, y)) {
        palette_close();
        return;
    }

    if (playlist_is_open())
        playlist_click(x, y);
}

void uidbg_hook_scroll(double x, double y, double dx, double dy)
{
    (void)x;
    (void)y;

    if (playlist_is_open())
        playlist_pan_by(dx, dy);
}

void uidbg_hook_motion(double x, double y)
{
    if (playlist_is_open())
        playlist_hover(x, y);
}

void uidbg_hook_seek(double seconds)
{
    player_seek_to(seconds);
}

static GtkWidget *find_text_entry(GtkWidget *widget)
{
    if (GTK_IS_ENTRY(widget) && gtk_widget_get_visible(widget))
        return widget;

    for (GtkWidget *child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_text_entry(child);

        if (found != NULL)
            return found;
    }

    return NULL;
}

void uidbg_hook_text(const gchar *text)
{
    GtkWidget *entry;

    if (!palette_is_open())
        return;

    entry = find_text_entry(app.window);
    if (entry == NULL)
        return;

    /* 设文字会触发 "changed"，面板自己重新过滤 / 选中第一项 */
    gtk_editable_set_text(GTK_EDITABLE(entry), text != NULL ? text : "");
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

    app.header_bar = NULL;
    app.header_title = NULL;
    app.canvas     = NULL;
    app.chrome     = NULL;
    app.wallgl     = NULL;
    app.bg_picture = NULL;
    app.bg_gl      = NULL;
}

static gboolean debug_open_wall(gpointer data);

static gboolean debug_open_palette(gpointer data)
{
    const gchar *query = g_getenv("FOLLOW_PALETTE_QUERY");

    (void)data;

    palette_open_query(query);

    {
        const gchar *steps = g_getenv("FOLLOW_PALETTE_STEPS");

        if (steps != NULL)
            palette_debug_steps(steps);
    }

    return G_SOURCE_REMOVE;
}

static gboolean debug_close_wall(gpointer data)
{
    (void)data;

    g_message("debug: closing wall (FOLLOW_WALL_AUTO_CLOSE)");
    playlist_close();

    if (g_getenv("FOLLOW_WALL_AUTO_REOPEN") != NULL)
        g_timeout_add(1200, debug_open_wall, NULL);

    return G_SOURCE_REMOVE;
}

/* 开发 / 性能测试用：自动开播（FOLLOW_AUTOPLAY） */
static gboolean debug_autoplay(gpointer data)
{
    (void)data;

    g_message("debug: autoplay (FOLLOW_AUTOPLAY)");
    player_toggle();

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

static void on_fullscreen_changed(GObject *object, GParamSpec *pspec,
                                  gpointer data)
{
    GtkWidget *handle = data;

    (void)pspec;

    if (handle != NULL) {
        gboolean fullscreen = gtk_window_is_fullscreen(GTK_WINDOW(object));
        GtkWidget *root = gtk_widget_get_parent(handle);

        gtk_widget_set_visible(handle, !fullscreen);

        if (root != NULL) {
            if (fullscreen)
                gtk_widget_remove_css_class(root, "window-frame");
            else
                gtk_widget_add_css_class(root, "window-frame");
        }
    }
}

#if !defined(_WIN32)
static void on_window_resize_press(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer data)
{
    GtkWidget     *window = app.window;
    int            w, h;
    const int      edge = 5;
    GdkSurfaceEdge e = (GdkSurfaceEdge)-1;
    GdkSurface    *surface;
    GdkDevice     *device;

    (void)n_press;
    (void)data;

    if (window == NULL)
        return;

    w = gtk_widget_get_width(window);
    h = gtk_widget_get_height(window);

    if (x < 0 || y < 0 || x > w || y > h)
        return;

    if (x <= edge)
        e = (y <= edge) ? GDK_SURFACE_EDGE_NORTH_WEST :
            (y >= h - edge) ? GDK_SURFACE_EDGE_SOUTH_WEST :
            GDK_SURFACE_EDGE_WEST;
    else if (x >= w - edge)
        e = (y <= edge) ? GDK_SURFACE_EDGE_NORTH_EAST :
            (y >= h - edge) ? GDK_SURFACE_EDGE_SOUTH_EAST :
            GDK_SURFACE_EDGE_EAST;
    else if (y <= edge)
        e = GDK_SURFACE_EDGE_NORTH;
    else if (y >= h - edge)
        e = GDK_SURFACE_EDGE_SOUTH;
    else
        return;

    surface = gtk_native_get_surface(gtk_widget_get_native(window));
    device  = gtk_gesture_get_device(GTK_GESTURE(gesture));

    if (surface == NULL || device == NULL)
        return;

    gdk_toplevel_begin_resize(
        GDK_TOPLEVEL(surface), e, device, 1, x, y,
        gtk_event_controller_get_current_event_time(
            GTK_EVENT_CONTROLLER(gesture)));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}
#endif

#if defined(_WIN32)
typedef struct {
    int  mode;      /* 1 = move, 2 = resize */
    int  edges;     /* 1 N, 2 S, 4 W, 8 E */
    RECT start;
} WindowDrag;

static WindowDrag g_window_drag;

static HWND window_hwnd(void)
{
    GdkSurface *surface;

    if (app.window == NULL)
        return NULL;

    surface = gtk_native_get_surface(gtk_widget_get_native(app.window));
    if (surface == NULL)
        return NULL;

    return (HWND)gdk_win32_surface_get_handle(surface);
}

static void win_drag_begin(GtkGestureDrag *gesture, double sx, double sy,
                           gpointer data)
{
    GtkWidget      *window = app.window;
    GtkWidget      *picked;
    GtkWidget      *handle;
    graphene_rect_t bounds;
    HWND            hwnd;
    int             w, h;
    const int       edge = 5;

    (void)data;

    g_window_drag.mode  = 0;
    g_window_drag.edges = 0;

    if (window == NULL)
        return;

    w = gtk_widget_get_width(window);
    h = gtk_widget_get_height(window);

    picked = gtk_widget_pick(window, sx, sy, GTK_PICK_DEFAULT);
    if (picked != NULL &&
        gtk_widget_get_ancestor(picked, GTK_TYPE_BUTTON) != NULL)
        return;

    if (sx <= edge)             g_window_drag.edges |= 4;
    if (sx >= w - edge)         g_window_drag.edges |= 8;
    if (sy <= edge)             g_window_drag.edges |= 1;
    if (sy >= h - edge)         g_window_drag.edges |= 2;

    if (g_window_drag.edges != 0) {
        g_window_drag.mode = 2;
    } else {
        handle = app.header_bar != NULL ? gtk_widget_get_parent(app.header_bar)
                                        : NULL;

        if (handle != NULL &&
            gtk_widget_compute_bounds(handle, window, &bounds) &&
            sy >= bounds.origin.y &&
            sy <= bounds.origin.y + bounds.size.height)
            g_window_drag.mode = 1;
    }

    if (g_window_drag.mode == 0)
        return;

    hwnd = window_hwnd();
    if (hwnd == NULL) {
        g_window_drag.mode = 0;
        return;
    }

    GetWindowRect(hwnd, &g_window_drag.start);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void win_drag_update(GtkGestureDrag *gesture, double ox, double oy,
                            gpointer data)
{
    HWND hwnd;
    int  x, y, w, h;
    const int min_w = 640;
    const int min_h = 360;

    (void)gesture;
    (void)data;

    if (g_window_drag.mode == 0)
        return;

    hwnd = window_hwnd();
    if (hwnd == NULL)
        return;

    x = g_window_drag.start.left;
    y = g_window_drag.start.top;
    w = g_window_drag.start.right  - g_window_drag.start.left;
    h = g_window_drag.start.bottom - g_window_drag.start.top;

    if (g_window_drag.mode == 1) {
        SetWindowPos(hwnd, NULL, x + (int)ox, y + (int)oy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    if (g_window_drag.edges & 4) { x += (int)ox; w -= (int)ox; }
    if (g_window_drag.edges & 8) { w += (int)ox; }
    if (g_window_drag.edges & 1) { y += (int)oy; h -= (int)oy; }
    if (g_window_drag.edges & 2) { h += (int)oy; }

    if (w < min_w) {
        if (g_window_drag.edges & 4)
            x -= min_w - w;
        w = min_w;
    }

    if (h < min_h) {
        if (g_window_drag.edges & 1)
            y -= min_h - h;
        h = min_h;
    }

    SetWindowPos(hwnd, NULL, x, y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void win_drag_end(GtkGestureDrag *gesture, double ox, double oy,
                         gpointer data)
{
    (void)gesture;
    (void)ox;
    (void)oy;
    (void)data;

    g_window_drag.mode  = 0;
    g_window_drag.edges = 0;
}
#endif

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
    app.header_bar = header_bar;
    app.header_title = gtk_label_new(cfg->title);
    gtk_widget_add_css_class(app.header_title, "title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header_bar), app.header_title);
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

    /* 歌词：GL 层只画歌词（字形纹理 + shader 辉光 / 模糊），
     * 背景透明，盖在主画布上。没有 GL 时 canvas 自己画。 */
    app.lyricgl = lyricgl_new();

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

    if (app.lyricgl != NULL)
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.lyricgl);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.wallgl);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.chrome);

    /* 命令面板放最上层，右下角；列表在输入框上方，向上展开。 */
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), palette_new());

    {
        GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget *handle   = gtk_window_handle_new();

        gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), header_bar);
        gtk_widget_set_hexpand(handle, TRUE);
        gtk_widget_set_hexpand(root_box, TRUE);
        gtk_widget_set_vexpand(root_box, TRUE);
        gtk_widget_set_hexpand(overlay, TRUE);
        gtk_widget_set_vexpand(overlay, TRUE);
        gtk_widget_add_css_class(root_box, "window-frame");

        gtk_box_append(GTK_BOX(root_box), handle);
        gtk_box_append(GTK_BOX(root_box), overlay);

        gtk_window_set_child(GTK_WINDOW(window), root_box);
        /* CSD 的 12px 阴影区在 DComp 下会变成黑边，直接去掉外框。 */
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

        gtk_widget_set_visible(handle,
                               !gtk_window_is_fullscreen(GTK_WINDOW(window)));
        g_signal_connect(window, "notify::fullscreened",
                         G_CALLBACK(on_fullscreen_changed), handle);
    }

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

#if defined(_WIN32)
    {
        GtkGesture *drag = gtk_gesture_drag_new();

        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag),
                                      GDK_BUTTON_PRIMARY);
        gtk_event_controller_set_propagation_phase(
            GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
        g_signal_connect(drag, "drag-begin",
                         G_CALLBACK(win_drag_begin), NULL);
        g_signal_connect(drag, "drag-update",
                         G_CALLBACK(win_drag_update), NULL);
        g_signal_connect(drag, "drag-end",
                         G_CALLBACK(win_drag_end), NULL);
        gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(drag));
    }
#else
    {
        GtkGesture *resize = gtk_gesture_click_new();

        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(resize),
                                      GDK_BUTTON_PRIMARY);
        gtk_event_controller_set_propagation_phase(
            GTK_EVENT_CONTROLLER(resize), GTK_PHASE_CAPTURE);
        g_signal_connect(resize, "pressed",
                         G_CALLBACK(on_window_resize_press), NULL);
        gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(resize));
    }
#endif

#if defined(_WIN32)
    g_signal_connect(window, "realize", G_CALLBACK(on_window_realize), NULL);
#endif

    keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
    gtk_widget_add_controller(window, keys);

    attach_wall_input(app.canvas);
    attach_wall_input(app.chrome);

    {
        GtkGesture *click = gtk_gesture_click_new();

        gtk_event_controller_set_propagation_phase(
            GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
        g_signal_connect(click, "pressed", G_CALLBACK(on_window_clicked), NULL);
        gtk_widget_add_controller(overlay, GTK_EVENT_CONTROLLER(click));
    }

    app.track        = -1;
    app.shown_second = -1;
    app.repeat       = cfg->repeat;
    app.shuffle      = cfg->shuffle;

    /* 启动时先把第 0 首的资料摆上去，按下 Space 才开始播放 */
    cover_set(config_track_at(cfg, 0));
    qrcard_update(config_track_at(cfg, 0));
    player_update_window_title();

    gtk_window_present(GTK_WINDOW(window));

    /* agent 调试通道：只有 FOLLOW_UIDBG 打开时才监听端口 */
    if (g_getenv("FOLLOW_UIDBG") != NULL)
        uidbg_start(window);

#if defined(_WIN32)
    g_idle_add(apply_window_edge_idle, window);
#endif

    app.tick_id = g_timeout_add(TICK_MS, on_tick, NULL);

    backdrop_push(cfg->width, cfg->height);
    app.bg_tick_id = g_timeout_add(33, backdrop_tick, NULL);

    /* 开发 / 性能测试用：自动开墙、定时退出。 */
    if (g_getenv("FOLLOW_WALL_AUTO") != NULL)
        g_timeout_add(1200, debug_open_wall, NULL);

    if (g_getenv("FOLLOW_AUTOPLAY") != NULL)
        g_timeout_add(2500, debug_autoplay, NULL);

    if (g_getenv("FOLLOW_PALETTE_AUTO") != NULL)
        g_timeout_add(1600, debug_open_palette, NULL);

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
    settings_load();

    cache_prune_prefix("cover", config_cover_max_bytes());
    resolvecache_prune();

    g_message("cache: cover %.1f MB / limit %.0f MB",
              (double)cache_prefix_bytes("cover") / 1048576.0,
              (double)config_cover_max_bytes() / 1048576.0);

    keymap_init();
    viz_configure(config_viz_effect(), config_viz_particles(), config_viz_gain());

    if (app.media.ready) {
        media_set_volume(&app.media, settings_volume());
        media_set_muted(&app.media, settings_muted());

        if (settings_output_device() != NULL)
            media_set_output_device(&app.media, settings_output_device());
    }

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
        g_source_remove(app.tick_id);    if (app.bg_tick_id != 0)
        g_source_remove(app.bg_tick_id);
    if (app.anim_id != 0) {
        g_source_remove(app.anim_id);
        app.anim_id = 0;
    }

    g_clear_pointer(&app.snapshot, cairo_surface_destroy);

    uidbg_stop();

    stats_save(&app.stats);
    stats_clear(&app.stats);

    settings_save();
    settings_clear();

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

    /* 调试用：FOLLOW_APP_ID_OVERRIDE 換个 app id，就能和已经在跑的那份并存。
     * GtkApplication 是单实例的，不带它时第二份会直接把活交给第一份就退出。 */
    {
        const gchar *own_id = g_getenv("FOLLOW_APP_ID_OVERRIDE");

        app_ = gtk_application_new(own_id != NULL && *own_id != '\0' ? own_id : FOLLOW_APP_ID,
                                   FOLLOW_APP_FLAGS);
    }

    if (app_ == NULL)
        return 1;

    g_signal_connect(app_, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app_, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app_, "shutdown", G_CALLBACK(shutdown_app), NULL);

    status = g_application_run(G_APPLICATION(app_), argc, argv);

    g_object_unref(app_);
    return status;
}
