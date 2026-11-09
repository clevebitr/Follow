#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <gtk/gtk.h>
#include "qrcode.h"
#include "media.h"
#include "config.h"

#define CSS_PATH    "src/style/style.css"
#define CONFIG_PATH "src/config.ini"

#define TICK_MS 200

static Media media;

static struct {
    GtkWidget *scale;      /* 进度条：GtkScale，可拖动 */
    GtkWidget *time_label; /* "00:12 / 04:01" */
    GtkWidget *cover;      /* 封面：叠在淡色衬底上的 GtkPicture */
    GtkWidget *cover_aspect; /* 把封面区锁成图片宽高比的 GtkAspectFrame */
    guint      tick_id;    /* 定时器 id */
    gboolean   updating;   /* 程序自己改进度时置位，避免反过来触发 seek */
    gboolean   has_track;  /* 已经加载到曲目且知道总时长 */
    gint       track;      /* 当前曲目在 config 里的下标，-1 = 还没播 */
} ui;

static void play_music(GtkButton *button, gpointer user_data);
static void stop_music(GtkButton *button, gpointer user_data);
static void pause_music(GtkButton *button, gpointer user_data);
static gboolean on_tick(gpointer data);
static void dump_tracks(void);
static void set_cover(const AudioTrack *track);
static GtkWidget *dbg_top, *dbg_overlay, *dbg_aspect, *dbg_qr;
static gboolean dbg_sizes(gpointer d)
{
    (void)d;
    g_print("top %dx%d | overlay %dx%d | aspect %dx%d | qr %dx%d | cover %dx%d",
            gtk_widget_get_width(dbg_top), gtk_widget_get_height(dbg_top),
            gtk_widget_get_width(dbg_overlay), gtk_widget_get_height(dbg_overlay),
            gtk_widget_get_width(dbg_aspect), gtk_widget_get_height(dbg_aspect),
            gtk_widget_get_width(dbg_qr), gtk_widget_get_height(dbg_qr),
            gtk_widget_get_width(ui.cover), gtk_widget_get_height(ui.cover));
    g_print(" ENDSIZES");
    return G_SOURCE_REMOVE;
}

static void startup(GtkApplication *app, gpointer user_data)
{
    GtkCssProvider *provider;

    (void)app;
    (void)user_data;

    if (!g_file_test(CSS_PATH, G_FILE_TEST_EXISTS))
        g_warning("CSS file is not found in: %s", CSS_PATH);

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, CSS_PATH);

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);

    if (media_init(&media, NULL) != 0)
        g_warning("Media engine init failed!");

    if (!g_file_test(CONFIG_PATH, G_FILE_TEST_EXISTS))
        g_warning("Config file is not found in: %s (using defaults)", CONFIG_PATH);
    else
        g_message("config: loading %s (cwd = %s)", CONFIG_PATH, g_get_current_dir());

    config_load(CONFIG_PATH);
    ui.track = -1;
    dump_tracks();
}

static void shutdown_app(GtkApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;

    if (ui.tick_id != 0) {
        g_source_remove(ui.tick_id);
        ui.tick_id = 0;
    }

    media_clear(&media);
    config_free();
}

static char *format_time(double seconds)
{
    int total;

    if (seconds < 0.0) seconds = 0.0;
    total = (int)(seconds + 0.5);

    return g_strdup_printf("%02d:%02d", total / 60, total % 60);
}

static void update_time_label(double pos, double dur)
{
    char *text = g_strdup_printf("%s / %s", format_time(pos), format_time(dur));

    gtk_label_set_text(GTK_LABEL(ui.time_label), text);
    g_free(text);
}

/*Every 200ms*/
static gboolean on_tick(gpointer data)
{
    double pos = media_position(&media);
    double dur = media_duration(&media);

    (void)data;

    update_time_label(pos, dur);

    if (ui.has_track) {
        ui.updating = TRUE;
        gtk_range_set_value(GTK_RANGE(ui.scale), pos);
        ui.updating = FALSE;
    }

    return G_SOURCE_CONTINUE;
}

/* Seeding the scale */
static void on_scale_value_changed(GtkRange *range, gpointer data)
{
    (void)data;

    if (ui.updating || !ui.has_track)
        return;

    media_seek(&media, gtk_range_get_value(range));
    update_time_label(gtk_range_get_value(range), media_duration(&media));
}

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *window;
    GtkWidget *header_bar;
    GtkWidget *box;
    GtkWidget *top;
    GtkWidget *frame;
    GtkWidget *overlay;
    GtkWidget *progress;
    GtkWidget *controls;
    GtkWidget *button;
    QRcode *code;
    const APPconfig *cfg = config_get();

    (void)user_data;

    window = gtk_application_window_new(app);

    header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    gtk_window_set_title(GTK_WINDOW(window), cfg->title);
    gtk_window_set_default_size(GTK_WINDOW(window), cfg->width, cfg->height);

    if (cfg->fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(window));

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    /* ---------- 上半部分：左二维码，右封面（进度条叠在封面上） ---------- */
    top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_vexpand(top, TRUE);

    code = qrcode_generate(cfg->qrcode_link);
    if (code != NULL) {
        GdkTexture *texture = qrcode_to_texture(code, 8, 4);

        QRcode_free(code);

        if (texture != NULL) {
            GtkWidget *picture =
                gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));

            gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
            gtk_picture_set_content_fit(GTK_PICTURE(picture),
                                        GTK_CONTENT_FIT_CONTAIN);
            gtk_widget_set_size_request(picture, 180, 180);
            gtk_widget_set_halign(picture, GTK_ALIGN_START);
            gtk_widget_set_valign(picture, GTK_ALIGN_CENTER);
            gtk_widget_set_hexpand(picture, FALSE);

            gtk_box_append(GTK_BOX(top), picture);
            dbg_qr = picture;

            g_object_unref(texture);
        } else {
            g_warning("Qrcode to texture failed!");
        }
    } else {
        g_warning("Qrcode generate failed!");
    }

    /* 封面：
     *   overlay (GtkOverlay)
     *   ├── cover_aspect   GtkAspectFrame：锁成图片宽高比，图片正好填满
     *   │    └── cover-frame  淡色衬底（padding 露出细微的一圈光）
     *   │         └── ui.cover  GtkPicture
     *   └── cover-progress  贴在底部、压在图片上的进度条 + 时间
     */
    ui.cover = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(ui.cover), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(ui.cover), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_hexpand(ui.cover, TRUE);
    gtk_widget_set_vexpand(ui.cover, TRUE);

    frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "cover-frame");
    gtk_box_append(GTK_BOX(frame), ui.cover);

    ui.cover_aspect = gtk_aspect_frame_new(0.5, 0.5, 1.0, FALSE);
    gtk_widget_set_hexpand(ui.cover_aspect, TRUE);
    gtk_widget_set_vexpand(ui.cover_aspect, TRUE);
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(ui.cover_aspect), frame);

    overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), ui.cover_aspect);

    progress = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(progress, "cover-progress");
    gtk_widget_set_halign(progress, GTK_ALIGN_FILL);
    gtk_widget_set_valign(progress, GTK_ALIGN_END);

    ui.scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.1);
    gtk_scale_set_digits(GTK_SCALE(ui.scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(ui.scale), FALSE);
    gtk_widget_set_hexpand(ui.scale, TRUE);
    gtk_widget_set_valign(ui.scale, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(ui.scale, FALSE);
    g_signal_connect(ui.scale, "value-changed",
                     G_CALLBACK(on_scale_value_changed), NULL);
    gtk_box_append(GTK_BOX(progress), ui.scale);

    ui.time_label = gtk_label_new("00:00 / 00:00");
    gtk_box_append(GTK_BOX(progress), ui.time_label);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), progress);
    gtk_box_append(GTK_BOX(top), overlay);
    dbg_top = top; dbg_overlay = overlay; dbg_aspect = ui.cover_aspect;

    gtk_box_append(GTK_BOX(box), top);

    
    controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(controls, GTK_ALIGN_CENTER);

    button = gtk_button_new_from_icon_name("media-playback-start-symbolic");
    g_signal_connect(button, "clicked", G_CALLBACK(play_music), NULL);
    gtk_box_append(GTK_BOX(controls), button);

	button = gtk_button_new_from_icon_name("media-playback-pause-symbolic");
	g_signal_connect(button, "clicked", G_CALLBACK(pause_music), &media);
	gtk_box_append(GTK_BOX(controls), button);

	button = gtk_button_new_from_icon_name("media-playback-stop-symbolic");
	g_signal_connect(button, "clicked", G_CALLBACK(stop_music), &media);
	gtk_box_append(GTK_BOX(controls), button);

    gtk_box_append(GTK_BOX(box), controls);

    set_cover(config_track_at(cfg, 0));

    gtk_window_set_child(GTK_WINDOW(window), box);

    ui.tick_id = g_timeout_add(TICK_MS, on_tick, NULL);

    gtk_window_present(GTK_WINDOW(window));
    g_timeout_add(1500, dbg_sizes, NULL);
}

static void dump_tracks(void)
{
    const APPconfig *cfg = config_get();

    g_message("config: %" G_GSIZE_FORMAT " track(s), window %dx%d%s",
              cfg->n_tracks, cfg->width, cfg->height,
              cfg->fullscreen ? " (fullscreen)" : "");

    for (gsize i = 0; i < cfg->n_tracks; i++) {
        const AudioTrack *t = config_track_at(cfg, i);

        g_message("track[%" G_GSIZE_FORMAT "] %s  link=%s%s  pic=%s%s  lrc=%s%s",
                  i, t->id,
                  t->link, t->link_remote ? " (remote)" : "",
                  t->pic != NULL ? t->pic : "-", t->pic_remote ? " (remote)" : "",
                  t->lrc != NULL ? t->lrc : "-", t->lrc_remote ? " (remote)" : "");
    }
}

/* 清空封面：只留淡色衬底 */
static void clear_cover(void)
{
    if (ui.cover == NULL)
        return;

    gtk_picture_set_paintable(GTK_PICTURE(ui.cover), NULL);

    if (ui.cover_aspect != NULL)
        gtk_aspect_frame_set_ratio(GTK_ASPECT_FRAME(ui.cover_aspect), 1.0);
}

/* 把曲目封面塞进 GtkPicture；远程封面等网络部分写好再接上 */
static void set_cover(const AudioTrack *track)
{
    g_autofree gchar *path = NULL;
    g_autoptr(GError) error = NULL;
    GdkTexture *texture = NULL;

    if (ui.cover == NULL)
        return;

    if (track == NULL || track->pic == NULL || *track->pic == '\0') {
        clear_cover();
        return;
    }

    if (track->pic_remote) {
        /* TODO: http_get() 拿到字节后 gdk_texture_new_from_bytes() */
        g_debug("cover: remote picture not implemented yet: %s", track->pic);
        clear_cover();
        return;
    }

    path = config_to_local_path(track->pic);
    if (path == NULL)
        return;

    /* gdk_texture_new_from_filename 吃路径字符串，new_from_file 要的是 GFile */
    texture = gdk_texture_new_from_filename(path, &error);
    if (texture == NULL) {
        g_warning("cover: cannot load %s: %s", path,
                  error != NULL ? error->message : "unknown error");
        clear_cover();
        return;
    }

    gtk_picture_set_paintable(GTK_PICTURE(ui.cover), GDK_PAINTABLE(texture));

    /* 封面区跟着图片的宽高比走，进度条才真的压在图片上 */
    if (ui.cover_aspect != NULL) {
        gint h = gdk_texture_get_height(texture);

        gtk_aspect_frame_set_ratio(GTK_ASPECT_FRAME(ui.cover_aspect),
                                   (double)gdk_texture_get_width(texture) /
                                   (double)(h > 0 ? h : 1));
    }

    g_object_unref(texture);
}

static void apply_track_duration(double dur)
{
    ui.has_track = (dur > 0.0);
    if (dur <= 0.0) dur = 1.0;

    ui.updating = TRUE;
    gtk_range_set_range(GTK_RANGE(ui.scale), 0.0, dur);
    gtk_range_set_value(GTK_RANGE(ui.scale), 0.0);
    ui.updating = FALSE;

    gtk_widget_set_sensitive(ui.scale, ui.has_track);
    update_time_label(0.0, dur);
}

/* 试播一条曲目。远程链接的网络部分还没写，直接跳过；成功返回 TRUE。 */
static gboolean play_track(const AudioTrack *track, gsize index)
{
    g_autofree gchar *local = NULL;

    if (track->link_remote) {
        g_warning("track %s: remote audio not implemented yet, skipped: %s",
                  track->id, track->link);
        return FALSE;
    }

    local = config_to_local_path(track->link);
    if (local == NULL || !g_file_test(local, G_FILE_TEST_EXISTS)) {
        g_autofree gchar *abs = local != NULL ? g_canonicalize_filename(local, NULL) : NULL;

        g_warning("track %s: audio file not found: %s (cwd = %s)",
                  track->id, abs != NULL ? abs : track->link, g_get_current_dir());
        return FALSE;
    }

    if (media_play(&media, local) != 0) {
        g_warning("track %s: cannot play %s", track->id, local);
        return FALSE;
    }

    ui.track = (gint)index;
    set_cover(track);
    apply_track_duration(media_duration(&media));

    g_message("play: track[%" G_GSIZE_FORMAT "] %s -> %s", index, track->id, local);
    return TRUE;
}

static void play_music(GtkButton *button, gpointer user_data)
{
    const APPconfig *cfg = config_get();

    (void)button;
    (void)user_data;

    if (!media.ready) {
        g_warning("Media engine not ready!");
        return;
    }

    /* 已经在放 / 暂停中：先处理成续播，不要从头重来 */
    if (media.has_sound) {
        if (!media_is_playing(&media))
            media_continue(&media);
        return;
    }

    if (cfg->n_tracks == 0) {
        g_warning("config: no playable [audioN] section in %s", CONFIG_PATH);
        return;
    }

    /* 按 [audioN] 的顺序找第一条能播的；失败时直接用当前曲目再试一次 */
    if (ui.track >= 0) {
        const AudioTrack *track = config_track_at(cfg, (gsize)ui.track);

        if (track != NULL && play_track(track, (gsize)ui.track))
            return;
    }

    for (gsize i = 0; i < cfg->n_tracks; i++) {
        const AudioTrack *track = config_track_at(cfg, i);

        if (track != NULL && play_track(track, i))
            return;
    }

    g_warning("Play music failed: none of the %" G_GSIZE_FORMAT
              " track(s) in %s is playable", cfg->n_tracks, CONFIG_PATH);
}

static void pause_music(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    if (!media.ready) {
        g_warning("Media engine not ready!");
        return;
    }

    if (media_is_playing(&media)) {
        media_pause(&media);
        return;
    }

    if (media_is_paused(&media)) {
        media_continue(&media);
        return;
    }
}

static void stop_music(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    if (!media.ready) {
        g_warning("Media engine not ready!");
        return;
    }

    /* 暂停状态下也要能停（media_is_playing() 此时是 false） */
    if (media.has_sound) {
        media_stop(&media);
        ui.has_track = FALSE;
        apply_track_duration(0.0);
    }
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

#if defined(_WIN32)
    /* 让 printf/g_print 的 UTF-8 中文在 Windows 控制台上正常显示 */
    SetConsoleOutputCP(CP_UTF8);
#endif

    app = gtk_application_new("org.github.clevebitr.follow",
                              G_APPLICATION_DEFAULT_FLAGS);
    if (app == NULL)
        return 1;

    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(shutdown_app), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}
