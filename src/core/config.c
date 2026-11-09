#include "config.h"

#include <math.h>
#include <string.h>

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

/* 歌词偏移：写毫秒或秒都行，也接受后缀。
 *   2500 / 2500ms / 2.5s  → 2500 毫秒
 *   -7.5  / -7.5s         → -7500 毫秒
 * 规则：有 ms/s 后缀按后缀；没有后缀时 |值| >= 100 当毫秒，否则当秒
 * （老写法 2500 仍然有效，手写 -7.5 也能用 —— 原来整数解析会直接把小数丢掉）。 */
static gint config_parse_offset_ms(const gchar *raw)
{
    g_autofree gchar *text = NULL;
    gchar            *end;
    gboolean          seconds;
    double            value;

    if (raw == NULL || *raw == '\0')
        return 0;

    text = g_strstrip(g_strdup(raw));
    value = g_ascii_strtod(text, &end);

    if (end == text)
        return 0;

    g_strstrip(end);

    if (*end != '\0') {
        if (g_ascii_strcasecmp(end, "ms") == 0)          seconds = FALSE;
        else if (g_ascii_strcasecmp(end, "s") == 0 ||
                 g_ascii_strcasecmp(end, "sec") == 0)    seconds = TRUE;
        else                                             return 0;
    } else {
        seconds = fabs(value) < 100.0;
    }

    return (gint)llround(seconds ? value * 1000.0 : value);
}


static void audio_track_clear(AudioTrack *track)
{
    g_free(track->id);
    g_free(track->link);
    g_free(track->title);
    g_free(track->artist);
    g_free(track->pic);
    g_free(track->lrc);
    g_free(track->page);
    g_free(track->platform);
    g_free(track->note);
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
    g_free(g_config.qrcode_label);
    g_free(g_config.note);

    g_config.width       = WINDOW_WIDTH;
    g_config.height      = WINDOW_HEIGHT;
    g_config.fullscreen  = FALSE;
    g_config.title       = g_strdup(WINDOW_TITLE);
    g_config.qrcode_link = g_strdup(QRCODE_LINK);
    g_config.qrcode_label = NULL;
    g_config.note        = NULL;
    g_config.repeat      = REPEAT_ALL;   /* 默认列表循环 */
    g_config.shuffle     = FALSE;
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
        g_sort_array(names, n_names, sizeof(gchar *), compare_track_groups, NULL);

    cfg->tracks = g_new0(AudioTrack, n_names);

    for (gsize i = 0; i < n_names; i++) {
        AudioTrack *track = &cfg->tracks[cfg->n_tracks];
        g_autofree gchar *link = config_get_string(kf, names[i], "link");

        if (link == NULL || *link == '\0')
            continue;

        track->id         = g_strdup(names[i]);
        track->link       = g_steal_pointer(&link);
        track->title      = config_get_string(kf, names[i], "title");
        track->artist     = config_get_string(kf, names[i], "artist");
        track->pic        = config_get_string(kf, names[i], "pic");
        track->lrc        = config_get_string(kf, names[i], "lrc");
        {
            g_autofree gchar *offset = config_get_string(kf, names[i], "lrc_offset");

            track->lrc_offset_ms = config_parse_offset_ms(offset);
        }
        track->page       = config_get_string(kf, names[i], "page");
        track->note       = config_get_string(kf, names[i], "note");
        track->platform    = config_get_string(kf, names[i], "platform");
        track->link_remote = config_is_remote(track->link);
        track->pic_remote  = config_is_remote(track->pic);
        track->lrc_remote  = config_is_remote(track->lrc);

        if (track->title != NULL && *track->title == '\0') g_clear_pointer(&track->title, g_free);
        if (track->artist != NULL && *track->artist == '\0') g_clear_pointer(&track->artist, g_free);
        if (track->pic != NULL && *track->pic == '\0') g_clear_pointer(&track->pic, g_free);
        if (track->lrc != NULL && *track->lrc == '\0') g_clear_pointer(&track->lrc, g_free);

        cfg->n_tracks++;
    }

    g_free(names);
}

const gchar *config_track_note(const APPconfig *cfg, const AudioTrack *track)
{
    if (track != NULL && track->note != NULL && *track->note != '\0')
        return track->note;

    if (cfg != NULL && cfg->note != NULL && *cfg->note != '\0')
        return cfg->note;

    return NULL;
}

const gchar *config_repeat_name(gint repeat)
{
    switch (repeat) {
    case REPEAT_OFF: return "off";
    case REPEAT_ONE: return "one";
    default:         return "all";
    }
}

const AudioTrack *config_track_at(const APPconfig *cfg, gsize index)
{
    if (cfg == NULL || index >= cfg->n_tracks)
        return NULL;

    return &cfg->tracks[index];
}

gchar *config_track_title(const AudioTrack *track)
{
    gchar *base;
    gchar *query;
    gchar *dot;

    if (track == NULL)
        return NULL;

    if (track->title != NULL && *track->title != '\0')
        return g_strdup(track->title);

    if (track->link == NULL)
        return g_strdup(track->id != NULL ? track->id : "unknown");

    base = g_path_get_basename(track->link);

    query = strchr(base, '?');         /* 远程 URL 的 query 不要 */
    if (query != NULL)
        *query = '\0';

    dot = strrchr(base, '.');          /* 去掉扩展名 */
    if (dot != NULL && dot != base)
        *dot = '\0';

    return base;
}

gchar *config_track_label(const AudioTrack *track)
{
    g_autofree gchar *title = config_track_title(track);

    if (track == NULL)
        return g_strdup("");

    if (track->artist != NULL && *track->artist != '\0' && title != NULL && *title != '\0')
        return g_strdup_printf("%s - %s", track->artist, title);

    return g_strdup(title != NULL ? title : "");
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

    {
        g_autofree gchar *label = config_get_string(kf, "qrcode", "label");

        g_free(g_config.qrcode_label);
        g_config.qrcode_label = (label != NULL && *label != '\0')
            ? g_steal_pointer(&label) : NULL;
    }

    {
        g_autofree gchar *note = config_get_string(kf, "note", "text");

        g_free(g_config.note);
        g_config.note = (note != NULL && *note != '\0') ? g_steal_pointer(&note) : NULL;
    }

    {
        g_autofree gchar *repeat = config_get_string(kf, "playback", "repeat");

        if (repeat != NULL) {
            if (g_ascii_strcasecmp(repeat, "off") == 0)        g_config.repeat = REPEAT_OFF;
            else if (g_ascii_strcasecmp(repeat, "all") == 0)   g_config.repeat = REPEAT_ALL;
            else if (g_ascii_strcasecmp(repeat, "one") == 0)   g_config.repeat = REPEAT_ONE;
        }

        g_config.shuffle = config_get_bool(kf, "playback", "shuffle", g_config.shuffle);
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
    g_free(g_config.qrcode_label);
    g_free(g_config.note);

    g_config = (APPconfig){ 0 };
    g_loaded = FALSE;
}
