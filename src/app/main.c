/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#if defined(_WIN32)
#include <gdk/win32/gdkwin32.h>
#endif

#include "backdrop.h"
#include "appicon.h"
#include "backup.h"
#include "backdropgl.h"
#include "cache.h"
#include "config.h"
#include "cover.h"
#include "covercache.h"
#include "details.h"
#include "http.h"
#include "keymap.h"
#include "lyricgl.h"
#include "lyricview.h"
#include "media.h"
#include "metadata.h"
#include "cmdbox.h"
#include "perf.h"
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
#include "webdavui.h"

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
        /* DComp 下透明阴影区会呈现黑边，关闭非客户区渲染。 */
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

static void on_window_realize_icon(GtkWidget *widget, gpointer data)
{
    (void)data;
    appicon_apply(widget);
}

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
    /* 画布不可见时不驱动歌词动画。 */
    gboolean lyric_anim  = fx_running(&app.fx_lyric) && app.canvas != NULL &&
                           gtk_widget_get_visible(app.canvas);
    gboolean next_anim   = stage_next_animating();
    /* 封面淡入期间保持重绘。 */
    gboolean cover_anim  = app.canvas != NULL &&
                           gtk_widget_get_visible(app.canvas) &&
                           covercache_animating();
    gboolean busy        = stage_anim || chrome_anim || lyric_anim ||
                           next_anim || cover_anim;

    (void)data;

    if (app.win_dragging)
        return G_SOURCE_CONTINUE;

    /* GL 墙由 frame clock 驱动，此定时器仅触发 queue_draw。 */
    if (wall_gpu && pending && app.wallgl != NULL)
        gtk_widget_queue_draw(app.wallgl);

    if (app.canvas != NULL && gtk_widget_get_visible(app.canvas) &&
        (stage_anim || lyric_anim || next_anim || cover_anim || wall_cpu))
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

/* 动态背景定时器；帧率取自 [ui] backdrop_fps。 */
static void backdrop_timer_start(void)
{
    gint fps = config_backdrop_fps();
    guint ms = (guint)MAX(4, 1000 / MAX(fps, 1));

    if (app.bg_tick_id != 0) {
        g_source_remove(app.bg_tick_id);
        app.bg_tick_id = 0;
    }

    app.bg_tick_id = g_timeout_add(ms, backdrop_tick, NULL);
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

    /* DComp 下 GtkGLArea 为原生子表面，隐藏时需真正 unmap；
 * 首次启动保留 2 秒用于预热。 */
    show_wallgl = wall_open ||
                  (!warm_done && now - warm_start < 2 * G_USEC_PER_SEC);

    if (app.wallgl != NULL)
        gtk_widget_set_visible(app.wallgl, show_wallgl);

    if (app.bg_gl != NULL)
        gtk_widget_set_visible(app.bg_gl, !wall_full && gpu_bg);

    if (app.bg_picture != NULL)
        gtk_widget_set_visible(app.bg_picture, !wall_full && !gpu_bg);

    if (app.canvas != NULL)
        gtk_widget_set_visible(app.canvas, !gpu_wall);

    if (app.lyricgl != NULL)
        gtk_widget_set_visible(app.lyricgl,
                               !wall_open && !app.help && !gpu_wall &&
                               !details_is_open());

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

    /* 切歌时加载元数据；状态变化时刷新详情页与封面。 */
    {
        static gint  meta_track = -2;
        static guint meta_ver   = 0;
        gint          shown = app.track >= 0 ? app.track : 0;
        const AudioTrack *track = config_track_at(config_get(), (gsize)shown);

        if (shown != meta_track) {
            meta_track = shown;
            metadata_load_track(track);
            meta_ver = metadata_version();

            cover_apply_metadata();

            if (details_is_open())
                ui_request_redraw();
        } else if (metadata_version() != meta_ver) {
            meta_ver = metadata_version();

            if (details_is_open())
                ui_request_redraw();

            cover_apply_metadata();
        }
    }

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

void ui_toggle_fullscreen(void)
{
    GtkWindow *window = GTK_WINDOW(app.window);

    if (gtk_window_is_fullscreen(window))
        gtk_window_unfullscreen(window);
    else
        gtk_window_fullscreen(window);
}

/* 打开配置文件：GIO → ShellExecuteW → notepad。 */
gchar *ui_open_config_file(void)
{
    g_autofree gchar *path = g_canonicalize_filename(config_active_path(), NULL);

    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        return g_strdup_printf("找不到配置文件：%s", path);

    {
        GdkDisplay        *display = gdk_display_get_default();
        GAppLaunchContext *context = display != NULL
            ? G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(display))
            : NULL;
        GError           *error = NULL;
        g_autofree gchar *uri   = g_filename_to_uri(path, NULL, NULL);
        gboolean          ok    = FALSE;

        if (uri != NULL)
            ok = g_app_info_launch_default_for_uri(uri, context, &error);

        g_clear_object(&context);
        g_clear_error(&error);

        if (ok)
            return NULL;
    }

#if defined(_WIN32)
    {
        gunichar2 *wpath = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
        gboolean   ok    = FALSE;

        if (wpath != NULL) {
            if ((INT_PTR)ShellExecuteW(NULL, L"open", (LPCWSTR)wpath, NULL,
                                       NULL, SW_SHOWNORMAL) > 32)
                ok = TRUE;
            else if ((INT_PTR)ShellExecuteW(NULL, L"open", L"notepad.exe",
                                            (LPCWSTR)wpath, NULL,
                                            SW_SHOWNORMAL) > 32)
                ok = TRUE;

            g_free(wpath);
        }

        if (ok)
            return NULL;
    }
#endif

    return g_strdup_printf("无法打开 %s：系统中没有可用的编辑器", path);
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

    cfg = config_load(config_active_path());

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

    backdrop_timer_start();

    app.chrome_dirty = TRUE;
    ui_request_redraw();
    ui_animate();

    g_message("config: reloaded %s (%" G_GSIZE_FORMAT " tracks)",
              config_active_path(), cfg->n_tracks);
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state, gpointer data)
{
    const gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    const double   seek  = shift ? 30.0 : 5.0;

    (void)controller;
    (void)keycode;
    (void)data;

    if (keymap_match(KEY_PERF, keyval, state)) {
        perf_toggle();
        return TRUE;
    }

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

            return FALSE;
        }

        if (ctrl_shift_p) {
            webdav_ui_close();
            palette_open();
            return TRUE;
        }
    }

    if (webdav_ui_is_open()) {
        if (keyval == GDK_KEY_Escape) {
            webdav_ui_close();
            return TRUE;
        }

        return FALSE;
    }

    if (details_is_open())
        return details_key(keyval, state);

    if (app.help) {
        if (keymap_match(KEY_HELP, keyval, state) ||
            keymap_match(KEY_BACK, keyval, state)) {
            app.help = FALSE;
            app.chrome_dirty = TRUE;
            ui_request_redraw();
            ui_sync_layers();
        } else if (keymap_match(KEY_QUIT, keyval, state)) {
            gtk_window_close(GTK_WINDOW(app.window));
        } else if (keymap_match(KEY_DETAILS, keyval, state)) {
            app.help = FALSE;
            details_open();
            ui_sync_layers();
        } else if (keymap_match(KEY_PLAYLIST, keyval, state)) {
            app.help = FALSE;
            app.chrome_dirty = TRUE;
            playlist_open_page();
            ui_sync_layers();
        }

        return TRUE;
    }

    if (playlist_is_open()) {
        if (keymap_match(KEY_PLAYLIST, keyval, state) ||
            keymap_match(KEY_BACK, keyval, state)) {
            playlist_close();
            return TRUE;
        }

        if (keymap_match(KEY_DETAILS, keyval, state)) {
            playlist_close();
            details_open();
            ui_sync_layers();
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
    if (keymap_match(KEY_QUEUE_PREV, keyval, state)) {
        player_queue_step(-1);
        return TRUE;
    }
    if (keymap_match(KEY_QUEUE_NEXT, keyval, state)) {
        player_queue_step(+1);
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
    if (keymap_match(KEY_DETAILS, keyval, state)) {
        details_open();
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
    GtkWidget *entry = NULL;

    if (!palette_is_open() && !webdav_ui_is_open())
        return;

    {
        GtkRoot *root = gtk_widget_get_root(app.window);
        GtkWidget *focus = root != NULL ? gtk_root_get_focus(root) : NULL;

        if (GTK_IS_ENTRY(focus) && gtk_widget_get_visible(focus))
            entry = focus;
    }

    if (entry == NULL)
        entry = find_text_entry(app.window);

    if (entry == NULL)
        return;

    gtk_editable_set_text(GTK_EDITABLE(entry), text != NULL ? text : "");
}

/* 墙的鼠标事件：GL 墙与 cairo 回退分别挂载控制器。 */
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

/* 窗口销毁时停止定时器并清空控件指针。 */
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

static gboolean debug_webdav(gpointer data)
{
    BackupOp op = (BackupOp)GPOINTER_TO_INT(data);

    stats_save(&app.stats);
    settings_save();
    backup_start(op);
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

/* 无边框窗口仅接管边缘缩放；标题栏拖动由 GtkWindowHandle 处理。 */
static void on_window_drag_begin(GtkGestureDrag *gesture, double x, double y,
                                 gpointer data)
{
    GtkWidget     *window = app.window;
    GdkSurfaceEdge  e = (GdkSurfaceEdge)-1;
    GdkSurface    *surface;
    GdkDevice     *device;
    int            w, h;
    const int      edge = 5;

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

    app.win_dragging = TRUE;
    lyricgl_drag_state(TRUE);
}

static void window_drag_finish(void)
{
    if (!app.win_dragging)
        return;

    app.win_dragging = FALSE;
    lyricgl_drag_state(FALSE);

    if (app.canvas != NULL)
        gtk_widget_queue_draw(app.canvas);
}

static void on_window_drag_end(GtkGestureDrag *gesture, double ox, double oy,
                               gpointer data)
{
    (void)gesture;
    (void)ox;
    (void)oy;
    (void)data;

    window_drag_finish();
}

static void on_window_drag_cancel(GtkGesture *gesture, GdkEventSequence *seq,
                                  gpointer data)
{
    (void)gesture;
    (void)seq;
    (void)data;

    window_drag_finish();
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
    app.header_bar = header_bar;
    app.header_title = gtk_label_new(cfg->title);
    gtk_widget_add_css_class(app.header_title, "title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header_bar), app.header_title);
    gtk_window_set_title(GTK_WINDOW(window), cfg->title);
    gtk_window_set_default_size(GTK_WINDOW(window), cfg->width, cfg->height);

    if (cfg->fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(window));

    app.bg_picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(app.bg_picture), GTK_CONTENT_FIT_FILL);
    gtk_picture_set_can_shrink(GTK_PICTURE(app.bg_picture), TRUE);
    gtk_widget_set_hexpand(app.bg_picture, TRUE);
    gtk_widget_set_vexpand(app.bg_picture, TRUE);
    gtk_widget_add_css_class(app.bg_picture, "bg-picture");

    app.canvas = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app.canvas, TRUE);
    gtk_widget_set_vexpand(app.canvas, TRUE);
    gtk_widget_add_css_class(app.canvas, "canvas");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.canvas), stage_render, NULL, NULL);

    app.wallgl = wallgl_new();

    app.lyricgl = lyricgl_new();

    app.chrome = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app.chrome, TRUE);
    gtk_widget_set_vexpand(app.chrome, TRUE);
    gtk_widget_set_can_target(app.chrome, TRUE);
    gtk_widget_add_css_class(app.chrome, "canvas");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.chrome),
                                   stage_chrome_render, NULL, NULL);

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

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), palette_new());
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), webdav_ui_new());

    app.perf = perf_new(window);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.perf);

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
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

        gtk_widget_set_visible(handle,
                               !gtk_window_is_fullscreen(GTK_WINDOW(window)));
        g_signal_connect(window, "notify::fullscreened",
                         G_CALLBACK(on_fullscreen_changed), handle);
    }

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    g_signal_connect(window, "realize", G_CALLBACK(on_window_realize_icon), NULL);

    {
        GtkGesture *resize = gtk_gesture_drag_new();

        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(resize),
                                      GDK_BUTTON_PRIMARY);
        gtk_event_controller_set_propagation_phase(
            GTK_EVENT_CONTROLLER(resize), GTK_PHASE_CAPTURE);
        g_signal_connect(resize, "drag-begin",
                         G_CALLBACK(on_window_drag_begin), NULL);
        g_signal_connect(resize, "drag-end",
                         G_CALLBACK(on_window_drag_end), NULL);
        g_signal_connect(resize, "cancel",
                         G_CALLBACK(on_window_drag_cancel), NULL);
        gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(resize));
    }

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
    app.queue_next   = -1;
    app.shown_second = -1;
    app.repeat       = cfg->repeat;
    app.shuffle      = cfg->shuffle;

    cover_set(config_track_at(cfg, 0));
    qrcard_update(config_track_at(cfg, 0));
    player_update_window_title();

    gtk_window_present(GTK_WINDOW(window));
    appicon_apply(window);

    if (g_getenv("FOLLOW_UIDBG") != NULL)
        uidbg_start(window);

#if defined(_WIN32)
    g_idle_add(apply_window_edge_idle, window);
#endif

    app.tick_id = g_timeout_add(TICK_MS, on_tick, NULL);

    backdrop_push(cfg->width, cfg->height);
    backdrop_timer_start();

    if (g_getenv("FOLLOW_WALL_AUTO") != NULL)
        g_timeout_add(1200, debug_open_wall, NULL);

    if (g_getenv("FOLLOW_AUTOPLAY") != NULL)
        g_timeout_add(2500, debug_autoplay, NULL);

    if (g_getenv("FOLLOW_PALETTE_AUTO") != NULL)
        g_timeout_add(1600, debug_open_palette, NULL);

    if (g_getenv("FOLLOW_WALL_AUTO_PAN") != NULL)
        g_timeout_add(1600, debug_pan_start, NULL);

    if (g_getenv("FOLLOW_WEBDAV_AUTO") != NULL) {
        const gchar *op = g_getenv("FOLLOW_WEBDAV_AUTO");

        if (g_strcmp0(op, "backup") == 0)
            g_timeout_add(1200, debug_webdav,
                          GINT_TO_POINTER(BACKUP_OP_BACKUP));
        else if (g_strcmp0(op, "restore") == 0)
            g_timeout_add(1200, debug_webdav,
                          GINT_TO_POINTER(BACKUP_OP_RESTORE));
    }

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

    if (!g_file_test(config_active_path(), G_FILE_TEST_EXISTS)) {
        g_warning("config file is not found in: %s (using defaults)",
                  config_active_path());
    } else {
        g_message("config: loading %s (cwd = %s)", config_active_path(),
                  g_get_current_dir());
    }

    config_load(config_active_path());
    metadata_init();
    settings_load();

    cache_prune_prefix("cover", config_cover_max_bytes());
    cache_prune_prefix("metacover", config_cover_max_bytes());
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
    g_message("keys: play=%s  next=%s  playlist=%s  details=%s  help=%s",
              keymap_label(KEY_PLAY_PAUSE), keymap_label(KEY_NEXT),
              keymap_label(KEY_PLAYLIST), keymap_label(KEY_DETAILS),
              keymap_label(KEY_HELP));

    /* 代理同时传给 curl 与 FFmpeg（后者只读环境变量）。 */
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

    perf_clear();
    appicon_clear();
    uidbg_stop();

    metadata_shutdown();

    stats_save(&app.stats);
    stats_clear(&app.stats);

    settings_save();

    if (backup_auto_enabled()) {
        backup_start(BACKUP_OP_BACKUP);
        backup_wait(5000);
        backup_cancel();
    }
    backup_shutdown();
    webdav_ui_clear();

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

    g_set_prgname(FOLLOW_APP_ID);
    g_set_application_name(WINDOW_TITLE);

#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);

    /* GTK 4.24 Win32 默认不创建 DirectComposition 设备，
 * 启用 dcomp 后才使用 GL 渲染器。 */
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

    /* 覆盖 app id 以便多实例调试。 */
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
