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
 *   backdrop   动态背景（模糊封面 + 漂移的种子色）
 *   qrcard     二维码槽、平台胶囊
 *   layout     一次绘制用到的所有几何量
 *   stage      主画面（大时钟 / 备注 / 歌词 / 右列 / 统计）
 *   help       F1 快捷键页
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <gtk/gtk.h>

#include "backdrop.h"
#include "config.h"
#include "cover.h"
#include "http.h"
#include "lyricview.h"
#include "media.h"
#include "player.h"
#include "qrcard.h"
#include "stage.h"
#include "stats.h"
#include "ui.h"

#define CSS_PATH    "src/style/style.css"
#define CONFIG_PATH "src/config.ini"
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
    gboolean busy;

    (void)data;

    if (app.canvas != NULL)
        gtk_widget_queue_draw(app.canvas);

    busy = fx_running(&app.fx_stage) || fx_running(&app.fx_bg);

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

static gboolean on_tick(gpointer data)
{
    gboolean playing, paused;
    double   position;
    int      second, width, height;

    (void)data;

    player_check_finished();

    playing = media_is_playing(&app.media);
    paused  = media_is_paused(&app.media);

    track_listening_time(playing);

    width  = gtk_widget_get_width(app.window);
    height = gtk_widget_get_height(app.window);

    if (width != app.last_width || height != app.last_height) {
        app.last_width  = width;
        app.last_height = height;
        ui_request_redraw();
    }

    position = media_position(&app.media);
    second   = (int)position;

    if (second != app.shown_second) {
        app.shown_second = second;
        ui_request_redraw();
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
        gtk_widget_queue_draw(app.canvas);
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

    if (app.help) {
        if (keyval == GDK_KEY_F1 || keyval == GDK_KEY_Escape) {
            app.help = FALSE;
            ui_request_redraw();
        } else if (keyval == GDK_KEY_q || keyval == GDK_KEY_Q) {
            gtk_window_close(GTK_WINDOW(app.window));
        }

        return TRUE;
    }

    switch (keyval) {
    case GDK_KEY_space:
        player_toggle();
        return TRUE;
    case GDK_KEY_Left:
        player_seek_by(-seek);
        return TRUE;
    case GDK_KEY_Right:
        player_seek_by(+seek);
        return TRUE;
    case GDK_KEY_n: case GDK_KEY_N:
        player_play_neighbour(+1);
        return TRUE;
    case GDK_KEY_p: case GDK_KEY_P:
        player_play_neighbour(-1);
        return TRUE;
    case GDK_KEY_r: case GDK_KEY_R:
        player_restart();
        return TRUE;
    case GDK_KEY_s: case GDK_KEY_S:
        player_stop();
        return TRUE;
    case GDK_KEY_bracketleft:            /* +250ms */
        lyricview_shift_offset(-250);
        return TRUE;
    case GDK_KEY_bracketright:           /* -250ms */
        lyricview_shift_offset(+250);
        return TRUE;
    case GDK_KEY_backslash:              /* reset config */
        lyricview_reset_offset();
        return TRUE;
    case GDK_KEY_l: case GDK_KEY_L:
        player_cycle_repeat();
        return TRUE;
    case GDK_KEY_h: case GDK_KEY_H:
        player_toggle_shuffle();
        return TRUE;
    case GDK_KEY_f: case GDK_KEY_F: case GDK_KEY_F11:
        toggle_fullscreen();
        return TRUE;
    case GDK_KEY_F1:
        app.help = TRUE;
        ui_request_redraw();
        return TRUE;
    case GDK_KEY_Escape:
        if (gtk_window_is_fullscreen(GTK_WINDOW(app.window)))
            gtk_window_unfullscreen(GTK_WINDOW(app.window));
        return TRUE;
    case GDK_KEY_q: case GDK_KEY_Q:
        gtk_window_close(GTK_WINDOW(app.window));
        return TRUE;
    default:
        return FALSE;
    }
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

    overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), app.bg_picture);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.canvas);
    gtk_window_set_child(GTK_WINDOW(window), overlay);

    keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
    gtk_widget_add_controller(window, keys);

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

    if (media_init(&app.media) != 0)
        g_warning("media engine init failed");

    if (!g_file_test(CONFIG_PATH, G_FILE_TEST_EXISTS)) {
        g_warning("config file is not found in: %s (using defaults)", CONFIG_PATH);
    } else {
        g_message("config: loading %s (cwd = %s)", CONFIG_PATH, g_get_current_dir());
    }

    config_load(CONFIG_PATH);

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
    qrcard_clear();
    config_free();

    http_global_cleanup();
}

int main(int argc, char **argv)
{
    GtkApplication *app_;
    int             status;

#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    app_ = gtk_application_new("org.github.clevebitr.follow", G_APPLICATION_DEFAULT_FLAGS);
    if (app_ == NULL)
        return 1;

    g_signal_connect(app_, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app_, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app_, "shutdown", G_CALLBACK(shutdown_app), NULL);

    status = g_application_run(G_APPLICATION(app_), argc, argv);

    g_object_unref(app_);
    return status;
}
