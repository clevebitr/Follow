#include <gtk/gtk.h>
#include "qrcode.h"
#include "media.h"
#include "config.h"

#define CSS_PATH    "src/style/style.css"
#define BGM_PATH    "src/assets/bgm.mp3"
#define CONFIG_PATH "src/config.ini"

#define TICK_MS 200

static Media media;

static struct {
    GtkWidget *scale;      /* 进度条：GtkScale，可拖动 */
    GtkWidget *time_label; /* "00:12 / 04:01" */
    guint      tick_id;    /* 定时器 id */
    gboolean   updating;   /* 程序自己改进度时置位，避免反过来触发 seek */
    gboolean   has_track;  /* 已经加载到曲目且知道总时长 */
} ui;

static void play_music(GtkButton *button, gpointer user_data);
static void stop_music(GtkButton *button, gpointer user_data);
static void pause_music(GtkButton *button, gpointer user_data);
static gboolean on_tick(gpointer data);
static void dump_tracks(void);

static void startup(GtkApplication *app, gpointer user_data)
{
    GtkCssProvider *provider;

    (void)app;
    (void)user_data;

    if (!g_file_test(CSS_PATH, G_FILE_TEST_EXISTS))
        g_warning("CSS file is not fond in: %s", CSS_PATH);

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, CSS_PATH);

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);

    if (!g_file_test(BGM_PATH, G_FILE_TEST_EXISTS))
        g_warning("Media file is not found in: %s", BGM_PATH);

    if (media_init(&media, NULL) != 0)
        g_warning("Media engine init failed!");

    /* 只加载一次，之后任何地方用 config_get() 拿同一份数据 */
    config_load(CONFIG_PATH);
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
            gtk_widget_set_vexpand(picture, TRUE);

            gtk_box_append(GTK_BOX(box), picture);

            g_object_unref(texture);
        } else {
            g_warning("Qrcode to texture failed!");
        }
    } else {
        g_warning("Qrcode generate failed!");
    }

    controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    button = gtk_button_new_from_icon_name("media-playback-start-symbolic");
    g_signal_connect(button, "clicked", G_CALLBACK(play_music), NULL);
    gtk_box_append(GTK_BOX(controls), button);

	button = gtk_button_new_from_icon_name("media-playback-pause-symbolic");
	g_signal_connect(button, "clicked", G_CALLBACK(pause_music), &media);
	gtk_box_append(GTK_BOX(controls), button);

	button = gtk_button_new_from_icon_name("media-playback-stop-symbolic");
	g_signal_connect(button, "clicked", G_CALLBACK(stop_music), &media);
	gtk_box_append(GTK_BOX(controls), button);

    ui.scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.1);
    gtk_scale_set_digits(GTK_SCALE(ui.scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(ui.scale), FALSE);
    gtk_widget_set_hexpand(ui.scale, TRUE);
    gtk_widget_set_sensitive(ui.scale, FALSE);
    g_signal_connect(ui.scale, "value-changed",
                     G_CALLBACK(on_scale_value_changed), NULL);
    gtk_box_append(GTK_BOX(controls), ui.scale);

    ui.time_label = gtk_label_new("00:00 / 00:00");
    gtk_box_append(GTK_BOX(controls), ui.time_label);

    gtk_box_append(GTK_BOX(box), controls);

    gtk_window_set_child(GTK_WINDOW(window), box);

    ui.tick_id = g_timeout_add(TICK_MS, on_tick, NULL);

    gtk_window_present(GTK_WINDOW(window));
}

static void dump_tracks(void)
{
    const APPconfig *cfg = config_get();

    for (gsize i = 0; i < cfg->n_tracks; i++) {
        const AudioTrack *t = config_track_at(cfg, i);

        g_debug("track[%" G_GSIZE_FORMAT "] %s  link=%s%s  pic=%s%s  lrc=%s%s",
                i, t->id,
                t->link, t->link_remote ? " (remote)" : "",
                t->pic != NULL ? t->pic : "-", t->pic_remote ? " (remote)" : "",
                t->lrc != NULL ? t->lrc : "-", t->lrc_remote ? " (remote)" : "");
    }
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
static gboolean play_track(const AudioTrack *track)
{
    g_autofree gchar *local = NULL;

    if (track->link_remote) {
        g_debug("skip track %s: remote audio not implemented yet", track->id);
        return FALSE;
    }

    local = config_to_local_path(track->link);
    if (local == NULL || !g_file_test(local, G_FILE_TEST_EXISTS)) {
        g_warning("Audio file not found: %s", track->link);
        return FALSE;
    }

    if (media_play(&media, local) != 0) {
        g_warning("Play music failed: %s", local);
        return FALSE;
    }

    apply_track_duration(media_duration(&media));
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

    /* 按 [audioN] 的顺序找第一条能播的 */
    for (gsize i = 0; i < cfg->n_tracks; i++) {
        const AudioTrack *track = config_track_at(cfg, i);

        if (track != NULL && play_track(track))
            return;
    }

    if (cfg->n_tracks > 0)
        g_warning("No playable local track in config, falling back to %s", BGM_PATH);

    if (media_play(&media, BGM_PATH) != 0) {
        g_warning("Play music failed!");
        return;
    }

    apply_track_duration(media_duration(&media));
}

static void pause_music(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    if (!media.ready) {
        g_warning("Media engine not ready!");
        return;
    }

    if (media_is_playing(&media))
	{
        media_pause(&media);
		return;
	}


	if (media_is_paused(&media))
	{
		media_continue(&media);
		return;
	}

}

static void stop_music(GtkButton *button, gpointer user_data)
{ 
	if (!media.ready) {
        g_warning("Media engine not ready!");
        return;
    }
	if (media_is_playing(&media))
		media_stop(&media);
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

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
