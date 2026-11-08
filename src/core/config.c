#include "config.h"

static APPconfig g_config;
static gboolean  g_loaded = FALSE;

gboolean config_is_remote(const gchar *uri)
{
    g_autofree gchar *scheme = NULL;

    if (uri == NULL || *uri == '\0')
        return FALSE;

    scheme = g_uri_parse_scheme(uri);
    if (scheme == NULL)
        return FALSE;

    return g_ascii_strcasecmp(scheme, "http") == 0 ||
           g_ascii_strcasecmp(scheme, "https") == 0;
}

gchar *config_to_local_path(const gchar *uri)
{
    if (uri == NULL || *uri == '\0')
        return NULL;

    if (g_str_has_prefix(uri, "file://"))
        return g_filename_from_uri(uri, NULL, NULL);

    return g_strdup(uri);
}

static gint audio_group_number(const gchar *group)
{
    const gchar *p;
    gint n = 0;

    if (group == NULL || g_ascii_strncasecmp(group, "audio", 5) != 0)
        return -1;

    p = group + 5;                         /* strlen("audio") */
    if (*p == '\0')                        /* 光秃秃的 [audio] 不算 */
        return -1;

    for (; *p != '\0'; p++) {
        if (!g_ascii_isdigit(*p))
            return -1;
        if (n > (G_MAXINT - 9) / 10)
            return -1;
        n = n * 10 + (*p - '0');
    }

    return n;
}

static gint compare_track_groups(gconstpointer a, gconstpointer b, gpointer data)
{
    const gchar *ga = *(const gchar *const *)a;
    const gchar *gb = *(const gchar *const *)b;

    (void)data;

    return audio_group_number(ga) - audio_group_number(gb);
}


static gint config_get_int(GKeyFile *kf, const gchar *group, const gchar *key,
                           gint fallback)
{
    g_autoptr(GError) error = NULL;
    gint value = g_key_file_get_integer(kf, group, key, &error);

    return error ? fallback : value;
}

static gboolean config_get_bool(GKeyFile *kf, const gchar *group, const gchar *key,
                                gboolean fallback)
{
    g_autoptr(GError) error = NULL;
    gboolean value = g_key_file_get_boolean(kf, group, key, &error);

    return error ? fallback : value;
}

static gchar *config_get_string(GKeyFile *kf, const gchar *group, const gchar *key)
{
    g_autoptr(GError) error = NULL;
    gchar *value = g_key_file_get_string(kf, group, key, &error);

    return error ? NULL : value;
}


static void audio_track_clear(AudioTrack *track)
{
    g_free(track->id);
    g_free(track->link);
    g_free(track->pic);
    g_free(track->lrc);
    *track = (AudioTrack){ 0 };
}

static void tracks_clear(APPconfig *cfg)
{
    for (gsize i = 0; i < cfg->n_tracks; i++)
        audio_track_clear(&cfg->tracks[i]);

    g_clear_pointer(&cfg->tracks, g_free);
    cfg->n_tracks = 0;
}

static void config_reset(void)
{
    tracks_clear(&g_config);

    g_free(g_config.title);
    g_free(g_config.qrcode_link);

    g_config.width       = WINDOW_WIDTH;
    g_config.height      = WINDOW_HEIGHT;
    g_config.fullscreen  = FALSE;
    g_config.title       = g_strdup(WINDOW_TITLE);
    g_config.qrcode_link = g_strdup(QRCODE_LINK);
}

const APPconfig *config_get(void)
{
    if (!g_loaded) {
        config_reset();
        g_loaded = TRUE;
    }

    return &g_config;
}


static void config_load_tracks(GKeyFile *kf, APPconfig *cfg)
{
    g_auto(GStrv) groups = NULL;        /* 由 keyfile 拥有内容，g_strfreev 释放外壳 */
    gchar       **names;                /* 只借 groups 里的指针，别用 g_strfreev */
    gsize         n_groups = 0;
    gsize         n_names = 0;

    groups = g_key_file_get_groups(kf, &n_groups);
    if (groups == NULL || n_groups == 0)
        return;

    names = g_new0(gchar *, n_groups);

    for (gsize i = 0; i < n_groups; i++) {
        if (audio_group_number(groups[i]) >= 0)
            names[n_names++] = groups[i];
    }

    if (n_names > 1)
        g_qsort_with_data(names, (gint)n_names, sizeof(gchar *),
                          compare_track_groups, NULL);

    cfg->tracks = g_new0(AudioTrack, n_names);

    for (gsize i = 0; i < n_names; i++) {
        AudioTrack *track = &cfg->tracks[cfg->n_tracks];
        g_autofree gchar *link = config_get_string(kf, names[i], "link");

        if (link == NULL || *link == '\0')
            continue;

        track->id         = g_strdup(names[i]);
        track->link       = g_steal_pointer(&link);
        track->pic        = config_get_string(kf, names[i], "pic");
        track->lrc        = config_get_string(kf, names[i], "lrc");
        track->link_remote = config_is_remote(track->link);
        track->pic_remote  = config_is_remote(track->pic);
        track->lrc_remote  = config_is_remote(track->lrc);

        if (track->pic != NULL && *track->pic == '\0') g_clear_pointer(&track->pic, g_free);
        if (track->lrc != NULL && *track->lrc == '\0') g_clear_pointer(&track->lrc, g_free);

        cfg->n_tracks++;
    }

    g_free(names);
}

const AudioTrack *config_track_at(const APPconfig *cfg, gsize index)
{
    if (cfg == NULL || index >= cfg->n_tracks)
        return NULL;

    return &cfg->tracks[index];
}


const APPconfig *config_load(const char *path)
{
    g_autoptr(GKeyFile) kf = g_key_file_new();

    config_reset();
    g_loaded = TRUE;

    if (path == NULL)
        return &g_config;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_debug("config: %s not usable, using defaults", path);
        return &g_config;
    }

    gint width  = config_get_int(kf, "window", "width", WINDOW_WIDTH);
    gint height = config_get_int(kf, "window", "height", WINDOW_HEIGHT);

    g_config.width      = (width  > 0) ? width  : WINDOW_WIDTH;
    g_config.height     = (height > 0) ? height : WINDOW_HEIGHT;
    g_config.fullscreen = config_get_bool(kf, "window", "fullscreen", FALSE);

    g_autofree gchar *title = config_get_string(kf, "window", "title");
    if (title != NULL && *title != '\0') {
        g_free(g_config.title);
        g_config.title = g_steal_pointer(&title);
    }

    g_autofree gchar *qr = config_get_string(kf, "qrcode", "link");
    if (qr != NULL && *qr != '\0') {
        g_free(g_config.qrcode_link);
        g_config.qrcode_link = g_steal_pointer(&qr);
    }

    config_load_tracks(kf, &g_config);

    g_debug("config: %" G_GSIZE_FORMAT " track(s) loaded", g_config.n_tracks);

    return &g_config;
}

void config_free(void)
{
    tracks_clear(&g_config);

    g_free(g_config.title);
    g_free(g_config.qrcode_link);

    g_config = (APPconfig){ 0 };
    g_loaded = FALSE;
}

void config_save_default(const char *path)
{
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autoptr(GError) error = NULL;

    g_key_file_set_integer(key_file, "window", "width", WINDOW_WIDTH);
    g_key_file_set_integer(key_file, "window", "height", WINDOW_HEIGHT);
    g_key_file_set_boolean(key_file, "window", "fullscreen", FALSE);
    g_key_file_set_string(key_file, "window", "title", WINDOW_TITLE);

    g_key_file_set_string(key_file, "qrcode", "link", QRCODE_LINK);

    g_key_file_set_comment(key_file, "audio1", NULL,
                           " 每个 [audioN] 是一项曲目，按 N 从小到大排序  ", NULL);
    g_key_file_set_string(key_file, "audio1", "link", "src/assets/bgm.mp3");
    g_key_file_set_string(key_file, "audio1", "pic", "");
    g_key_file_set_string(key_file, "audio1", "lrc", "");

    g_key_file_set_string(key_file, "audio2", "link", "https://example.com/song.mp3");
    g_key_file_set_string(key_file, "audio2", "pic", "https://example.com/cover.jpg");
    g_key_file_set_string(key_file, "audio2", "lrc", "https://example.com/song.lrc");

    if (!g_key_file_save_to_file(key_file, path, &error))
        g_warning("Failed to save default config: %s", error->message);
}
