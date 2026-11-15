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


/* GKeyFile 不支持行内注释：`proxy = http://... ; 说明` 会把“; 说明”也算进值里，
 * 结果就是代理地址非法、请求直接失败（表面上看就像“代理没生效”）。
 * 这里统一砍掉值尾巴上的行内注释：只当 `;` / `#` 前面是空白、后面也是空白
 * 或行尾时才当注释，所以 `Mozilla/4.0 (compatible; MSIE 6.0)` 这种不会被误伤。 */
static void config_strip_inline_comment(gchar *value)
{
    gchar *p;

    if (value == NULL)
        return;

    for (p = value; *p != '\0'; p++) {
        if ((*p != ';' && *p != '#'))
            continue;
        if (p == value || !g_ascii_isspace(p[-1]))
            continue;
        if (p[1] != '\0' && !g_ascii_isspace(p[1]))
            continue;

        *p = '\0';
        break;
    }

    g_strstrip(value);
}

static gchar *config_get_string(GKeyFile *kf, const gchar *group, const gchar *key)
{
    g_autoptr(GError) error = NULL;
    gchar *value = g_key_file_get_string(kf, group, key, &error);

    if (error != NULL)
        return NULL;

    config_strip_inline_comment(value);

    return value;
}

/* 整数 / 布尔也先当字符串读，这样 `timeout = 15   ; 秒` 同样能用 */
static gint config_get_int(GKeyFile *kf, const gchar *group, const gchar *key,
                           gint fallback)
{
    g_autofree gchar *raw = config_get_string(kf, group, key);
    gchar            *end = NULL;
    gint64            value;

    if (raw == NULL || *raw == '\0')
        return fallback;

    value = g_ascii_strtoll(raw, &end, 10);

    if (end == raw || (end != NULL && *g_strstrip(end) != '\0'))
        return fallback;

    return (value < G_MININT || value > G_MAXINT) ? fallback : (gint)value;
}

static gboolean config_get_bool(GKeyFile *kf, const gchar *group, const gchar *key,
                                gboolean fallback)
{
    g_autofree gchar *raw = config_get_string(kf, group, key);

    if (raw == NULL || *raw == '\0')
        return fallback;

    if (g_ascii_strcasecmp(raw, "true") == 0 || g_ascii_strcasecmp(raw, "1") == 0 ||
        g_ascii_strcasecmp(raw, "yes") == 0 || g_ascii_strcasecmp(raw, "on") == 0)
        return TRUE;

    if (g_ascii_strcasecmp(raw, "false") == 0 || g_ascii_strcasecmp(raw, "0") == 0 ||
        g_ascii_strcasecmp(raw, "no") == 0 || g_ascii_strcasecmp(raw, "off") == 0)
        return FALSE;

    return fallback;
}

static double config_get_double(GKeyFile *kf, const gchar *group, const gchar *key,
                                double fallback)
{
    g_autofree gchar *raw = config_get_string(kf, group, key);
    gchar            *end = NULL;
    double            value;

    if (raw == NULL || *raw == '\0')
        return fallback;

    value = g_ascii_strtod(raw, &end);

    if (end == raw || (end != NULL && *g_strstrip(end) != '\0'))
        return fallback;

    return value;
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
    g_free(g_config.cache_dir);
    g_free(g_config.user_agent);
    g_free(g_config.viz_effect);

    for (int i = 0; i < KEY_ACTION_COUNT; i++)
        g_free(g_config.key_bind[i]);

    g_config.width       = WINDOW_WIDTH;
    g_config.height      = WINDOW_HEIGHT;
    g_config.fullscreen  = FALSE;
    g_config.title       = g_strdup(WINDOW_TITLE);
    g_config.qrcode_link = g_strdup(QRCODE_LINK);
    g_config.qrcode_label = NULL;
    g_config.note        = NULL;
    g_config.repeat      = REPEAT_ALL;   /* 默认列表循环 */
    g_config.shuffle     = FALSE;
    g_config.cache_dir   = NULL;         /* NULL = <用户缓存目录>/follow */
    g_config.resolve_ttl    = 2700;      /* 45 分钟 */
    g_config.audio_cache    = FALSE;     /* 默认不落音频 */
    g_config.audio_max_mb   = 512;
    g_config.cover_max_mb   = 128;
    g_config.animations     = TRUE;
    g_config.backdrop_fps   = 30;
    g_config.viz_effect  = g_strdup("dust");
    g_config.viz_particles = 150;
    g_config.viz_gain      = 1.0;
    g_config.user_agent  = NULL;         /* NULL = 内置的浏览器 UA */
    g_config.proxy       = NULL;         /* NULL = 直连 */
    g_config.http_timeout    = 0;        /* 0 = HTTP_DEFAULT_TIMEOUT */
    g_config.connect_timeout = 0;        /* 0 = HTTP_DEFAULT_CONNECT_TIMEOUT */

    memset(g_config.key_bind, 0, sizeof(g_config.key_bind));
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

    /* [cache] dir：缓存目录。支持 ~ 和相对路径，留空就用 <用户缓存>/follow */
    {
        g_autofree gchar *dir = config_get_string(kf, "cache", "dir");

        g_free(g_config.cache_dir);
        g_config.cache_dir = (dir != NULL && *dir != '\0') ? g_steal_pointer(&dir) : NULL;

        g_config.resolve_ttl  = config_get_int(kf, "cache", "resolve_ttl",
                                               g_config.resolve_ttl);
        g_config.audio_cache  = config_get_bool(kf, "cache", "audio",
                                                g_config.audio_cache);
        g_config.audio_max_mb = config_get_int(kf, "cache", "audio_max_mb",
                                               g_config.audio_max_mb);
        g_config.cover_max_mb = config_get_int(kf, "cache", "cover_max_mb",
                                               g_config.cover_max_mb);
    }

    /* [net]：UA 和两个超时；没写就用内置默认 */
    {
        g_autofree gchar *ua = config_get_string(kf, "net", "user_agent");

        g_free(g_config.user_agent);
        g_config.user_agent = (ua != NULL && *ua != '\0') ? g_steal_pointer(&ua) : NULL;

        g_config.http_timeout    = config_get_int(kf, "net", "timeout", 0);
        g_config.connect_timeout = config_get_int(kf, "net", "connect_timeout", 0);
    }

    /* [ui] 动效总开关 / 动态背景刷新率 */
    g_config.animations = config_get_bool(kf, "ui", "animations", TRUE);
    g_config.backdrop_fps = config_get_int(kf, "ui", "backdrop_fps",
                                           g_config.backdrop_fps);

    /* [net] proxy：YouTube 这类在国内需要代理，curl 和 FFmpeg 都用它 */
    {
        g_autofree gchar *proxy = config_get_string(kf, "net", "proxy");

        g_free(g_config.proxy);
        g_config.proxy = (proxy != NULL && *proxy != '\0') ? g_steal_pointer(&proxy) : NULL;
    }

    /* [visualizer] 背景粒子：off / dust / warp / flow */
    {
        g_autofree gchar *effect = config_get_string(kf, "visualizer", "effect");

        g_free(g_config.viz_effect);
        g_config.viz_effect = (effect != NULL && *effect != '\0')
            ? g_steal_pointer(&effect) : g_strdup("dust");

        g_config.viz_particles = config_get_int(kf, "visualizer", "particles", 150);
        g_config.viz_gain      = config_get_double(kf, "visualizer", "gain", 1.0);
    }

    /* [keys]：值原样留着，由 keymap 统一解析；不认识的键名只记一条日志 */
    {
        gsize   n_keys = 0;
        g_auto(GStrv) keys = g_key_file_get_keys(kf, "keys", &n_keys, NULL);

        if (keys != NULL) {
            for (gsize i = 0; i < n_keys; i++) {
                gint action = keymap_action_from_name(keys[i]);

                if (action < 0) {
                    g_debug("keys: unknown action \"%s\"", keys[i]);
                    continue;
                }

                g_free(g_config.key_bind[action]);
                g_config.key_bind[action] = config_get_string(kf, "keys", keys[i]);
            }
        }
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
    g_free(g_config.cache_dir);
    g_free(g_config.user_agent);
    g_free(g_config.proxy);
    g_free(g_config.viz_effect);

    for (int i = 0; i < KEY_ACTION_COUNT; i++)
        g_free(g_config.key_bind[i]);

    g_config = (APPconfig){ 0 };
    g_loaded = FALSE;
}

const gchar *config_user_agent(void)
{
    const APPconfig *cfg = config_get();

    if (cfg->user_agent != NULL && *cfg->user_agent != '\0')
        return cfg->user_agent;

    return HTTP_DEFAULT_USER_AGENT;
}

gint config_http_timeout(void)
{
    gint seconds = config_get()->http_timeout;

    return seconds > 0 ? seconds : HTTP_DEFAULT_TIMEOUT;
}

gint config_http_connect_timeout(void)
{
    gint seconds = config_get()->connect_timeout;

    return seconds > 0 ? seconds : HTTP_DEFAULT_CONNECT_TIMEOUT;
}

const gchar *config_proxy(void)
{
    const APPconfig *cfg = config_get();

    return (cfg->proxy != NULL && *cfg->proxy != '\0') ? cfg->proxy : NULL;
}

const gchar *config_viz_effect(void)
{
    const APPconfig *cfg = config_get();

    return (cfg->viz_effect != NULL && *cfg->viz_effect != '\0')
        ? cfg->viz_effect : "dust";
}

gint config_viz_particles(void)
{
    gint n = config_get()->viz_particles;

    return n > 0 ? MIN(n, 400) : 150;
}

double config_viz_gain(void)
{
    double gain = config_get()->viz_gain;

    if (!isfinite(gain) || gain <= 0.0)
        return 1.0;

    return CLAMP(gain, 0.2, 4.0);
}

const gchar *config_key_binding(KeyAction action)
{
    if (action < 0 || action >= KEY_ACTION_COUNT)
        return NULL;

    return config_get()->key_bind[action];
}

gint config_resolve_ttl(void)
{
    gint ttl = config_get()->resolve_ttl;

    return CLAMP(ttl, 0, 86400);
}

gboolean config_audio_cache(void)
{
    return config_get()->audio_cache;
}

guint64 config_audio_max_bytes(void)
{
    gint mb = config_get()->audio_max_mb;

    if (mb <= 0)
        mb = 512;

    return (guint64)CLAMP(mb, 16, 16384) * 1024ULL * 1024ULL;
}

guint64 config_cover_max_bytes(void)
{
    gint mb = config_get()->cover_max_mb;

    if (mb <= 0)
        mb = 128;

    return (guint64)CLAMP(mb, 16, 8192) * 1024ULL * 1024ULL;
}

gboolean config_animations(void)
{
    return config_get()->animations;
}

void config_set_animations(gboolean on)
{
    g_config.animations = on ? TRUE : FALSE;
}

gint config_backdrop_fps(void)
{
    gint fps = config_get()->backdrop_fps;

    if (fps < 10)
        fps = 30;

    return CLAMP(fps, 10, 240);
}
