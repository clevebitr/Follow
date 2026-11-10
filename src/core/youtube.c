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

#include <string.h>

#include <json-glib/json-glib.h>

#include "config.h"
#include "http.h"
#include "youtube.h"

/* InnerTube：YouTube 自家网页 / App 的播放接口。
 * key 是网页端公开的那把（所有免登录播放器都在用）。 */
#define YT_PLAYER_URL "https://www.youtube.com/youtubei/v1/player?prettyPrint=false"

/* 每个客户端拿到的数据不一样：网页端现在基本只给 signatureCipher（要跑 JS 解密），
 * 我们解不了；越像"手机 App"的客户端越可能直接给明文 url。
 * 所以按"最可能给明文 url"的顺序排，网页客户端放最后兜底。 */
typedef struct {
    const char *name;       /* clientName */
    const char *version;    /* clientVersion */
    const char *key;        /* X-Youtube-Client-Name */
    const char *ua;
    const char *extra;      /* 拼进 client 对象的额外字段（含前导逗号） */
} YtClient;

static const YtClient g_clients[] = {
    /* 头显版：目前给明文 url 最稳定的一路 */
    { "ANDROID_VR", "1.60.19", "28",
      "com.google.android.apps.youtube.vr.oculus/1.60.19 "
      "(Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip",
      ",\"androidSdkVersion\":32,\"deviceMake\":\"Oculus\",\"deviceModel\":\"Quest 3\","
      "\"osName\":\"Android\",\"osVersion\":\"12L\"" },

    /* iOS：同样常给明文 url */
    { "IOS", "20.10.4", "5",
      "com.google.ios.youtube/20.10.4 (iPhone16,3; U; CPU iOS 18_1_0 like Mac OS X;)",
      ",\"deviceMake\":\"Apple\",\"deviceModel\":\"iPhone16,3\","
      "\"osName\":\"iPhone\",\"osVersion\":\"18.1.0.22B83\"" },

    /* 电视端嵌入式播放器 */
    { "TVHTML5_SIMPLY_EMBEDDED_PLAYER", "2.0", "85",
      "Mozilla/5.0 (PlayStation; PlayStation 4/12.00) AppleWebKit/605.1.15 "
      "(KHTML, like Gecko) Version/16.0 Safari/605.1.15",
      ",\"clientScreen\":\"EMBED\",\"clientFormFactor\":\"UNKNOWN_FORM_FACTOR\"" },

    /* 网页端：经常需要签名解密 / PO token，只当兜底 */
    { "WEB", "2.20241202.01.00", "1",
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/124.0 Safari/537.36",
      "" },
};

#define YT_CLIENT_COUNT ((int)G_N_ELEMENTS(g_clients))

/* 我们要从 url 里摘掉的 query 参数：
 *   range / rn / rbuf 是"只给一段"的分块提示，留着 FFmpeg 只能拿到开头一小段；
 *   signature / expire / ip / id 那些必须原样保留。 */
static const char *const g_drop_params[] = { "range", "rn", "rbuf", NULL };

/* ---------- 小工具 ---------- */

static const char *obj_str(JsonObject *obj, const char *member)
{
    JsonNode *node;

    if (obj == NULL)
        return NULL;

    node = json_object_get_member(obj, member);
    if (node == NULL || json_node_get_value_type(node) != G_TYPE_STRING)
        return NULL;

    return json_node_get_string(node);
}

static gint64 obj_int(JsonObject *obj, const char *member, gint64 fallback)
{
    JsonNode *node;

    if (obj == NULL)
        return fallback;

    node = json_object_get_member(obj, member);
    if (node == NULL)
        return fallback;

    if (json_node_get_value_type(node) == G_TYPE_INT64)
        return json_node_get_int(node);
    if (json_node_get_value_type(node) == G_TYPE_DOUBLE)
        return (gint64)json_node_get_double(node);

    return fallback;
}

/* json-glib 的 *_member 取不到成员时会打 CRITICAL，先查一下再取 */
static JsonObject *obj_object(JsonObject *obj, const char *member)
{
    JsonNode *node;

    if (obj == NULL || !json_object_has_member(obj, member))
        return NULL;

    node = json_object_get_member(obj, member);

    return (node != NULL && JSON_NODE_HOLDS_OBJECT(node)) ? json_node_get_object(node) : NULL;
}

static JsonArray *obj_array(JsonObject *obj, const char *member)
{
    JsonNode *node;

    if (obj == NULL || !json_object_has_member(obj, member))
        return NULL;

    node = json_object_get_member(obj, member);

    return (node != NULL && JSON_NODE_HOLDS_ARRAY(node)) ? json_node_get_array(node) : NULL;
}

/* 11 位 base64url 风格的 videoId */
static gboolean valid_id(const gchar *s, gsize len)
{
    if (s == NULL || len != 11)
        return FALSE;

    for (gsize i = 0; i < len; i++) {
        if (!g_ascii_isalnum(s[i]) && s[i] != '-' && s[i] != '_')
            return FALSE;
    }

    return TRUE;
}

static gchar *id_at(const gchar *p)
{
    return valid_id(p, 11) ? g_strndup(p, 11) : NULL;
}

/* "v=ID" 里 v 前面必须是 ? 或 &，否则 "sv=" / "rv=" 这种会被误判 */
static gboolean is_param_start(const gchar *url, const gchar *p)
{
    return p > url && (p[-1] == '?' || p[-1] == '&');
}

gchar *youtube_video_id(const gchar *url)
{
    static const char *const paths[] = { "/shorts/", "/embed/", "/live/", "/v/", NULL };
    const gchar *p;

    if (url == NULL || *url == '\0')
        return NULL;

    if (valid_id(url, strlen(url)))              /* 光秃秃的 ID */
        return g_strdup(url);

    while ((p = strstr(url, "v=")) != NULL) {
        if (is_param_start(url, p)) {
            gchar *id = id_at(p + 2);

            if (id != NULL)
                return id;
        }

        url = p + 2;
    }

    {
        const gchar *base = strstr(url, "youtu.be/");

        if (base != NULL) {
            gchar *id = id_at(base + strlen("youtu.be/"));

            if (id != NULL)
                return id;
        }
    }

    for (int i = 0; paths[i] != NULL; i++) {
        const gchar *path = strstr(url, paths[i]);

        if (path != NULL) {
            gchar *id = id_at(path + strlen(paths[i]));

            if (id != NULL)
                return id;
        }
    }

    return NULL;
}

/* 去掉分块参数，其余原样保留 */
static gchar *url_without_chunk_params(const gchar *url)
{
    const gchar *q = strchr(url, '?');
    g_autofree gchar *base = NULL;
    GString   *out;
    gchar    **parts;

    if (q == NULL)
        return g_strdup(url);

    base = g_strndup(url, (gsize)(q - url));
    out  = g_string_new(base);
    parts = g_strsplit(q + 1, "&", -1);

    for (gsize i = 0; parts[i] != NULL; i++) {
        const gchar *eq = strchr(parts[i], '=');
        gsize        klen = eq != NULL ? (gsize)(eq - parts[i]) : strlen(parts[i]);
        gboolean     drop = FALSE;

        if (klen == 0)
            continue;

        for (int d = 0; g_drop_params[d] != NULL; d++) {
            if (strlen(g_drop_params[d]) == klen &&
                strncmp(parts[i], g_drop_params[d], klen) == 0) {
                drop = TRUE;
                break;
            }
        }

        if (drop)
            continue;

        g_string_append_c(out, out->len == (gsize)(q - url) ? '?' : '&');
        g_string_append(out, parts[i]);
    }

    g_strfreev(parts);

    return g_string_free(out, FALSE);
}

static gboolean url_has_param(const gchar *url, const char *name)
{
    const gchar *p = url;
    gsize        len = strlen(name);

    while ((p = strchr(p, name[0])) != NULL) {
        if ((p == url || p[-1] == '&' || p[-1] == '?') &&
            strncmp(p, name, len) == 0 && p[len] == '=')
            return TRUE;

        p++;
    }

    return FALSE;
}

/* ---------- 播放接口的 JSON ---------- */

typedef struct {
    gchar   *url;
    gint64   bitrate;
    gboolean throttled;   /* url 带 n=：会被限速，尽量别选 */
    gboolean restricted;  /* url 带 rqh=1：只能取开头约 1MB，seek 直接 403，尽量别选 */
    const gchar *mime;    /* 指向 json，只读 */
} Pick;

static void pick_free(Pick *p)
{
    g_free(p->url);
    *p = (Pick){ 0 };
}

/* 一条格式里能拿到的明文 url：优先 url，其次从 cipher 里拆（但 s= 要 JS 解密，
 * 我们做不了，所以只看不带 s 的那种）。 */
static gchar *format_url(JsonObject *fmt)
{
    const char *url = obj_str(fmt, "url");

    if (url != NULL && *url != '\0')
        return g_strdup(url);

    return NULL;
}

static void consider(Pick *best, JsonObject *fmt, gboolean audio_only)
{
    const char *mime = obj_str(fmt, "mimeType");
    g_autofree gchar *url = NULL;
    gint64      bitrate;
    gboolean    throttled;
    gboolean    restricted;

    if (mime == NULL)
        return;

    if (audio_only) {
        if (!g_str_has_prefix(mime, "audio/"))
            return;
    } else {
        /* 非自适应列表是音视频合一的；只要它确实带音频 */
        if (!g_str_has_prefix(mime, "video/"))
            return;
    }

    url = format_url(fmt);
    if (url == NULL)
        return;                      /* 只有 signatureCipher：跳过，换下一个客户端 */

    throttled  = url_has_param(url, "n");
    restricted = url_has_param(url, "rqh");
    bitrate    = obj_int(fmt, "bitrate", 0);

    if (best->url != NULL) {
        /* 排序优先级：不带 rqh（能随便 seek）> 不带 n（不限速）> 码率。
         * 注意：url 是 g_autofree，不替换时直接 return 让它自己释放，
         * 别再手写 g_free(url) —— 那会释放两次，直接闪退。 */
        if (best->restricted == restricted) {
            if (best->throttled != throttled) {
                if (best->throttled == FALSE)
                    return;          /* 现有的不限速，不换 */
            } else if (bitrate <= best->bitrate) {
                return;              /* 同档：码率不更高就不换 */
            }
        } else if (best->restricted == FALSE) {
            return;                  /* 现有的更自由，不换 */
        }
    }

    pick_free(best);
    best->url        = g_steal_pointer(&url);
    best->bitrate    = bitrate;
    best->throttled  = throttled;
    best->restricted = restricted;
    best->mime       = mime;
}

/* 从 player 接口的 JSON 里挑一条音频。
 * 成功返回 0；失败返回 -1 并给出原因（playability / 只有加密流 / 没有音频）。
 * 非 static 是为了离线测试 —— 不给外面用，别在别的 .c 里调。 */
int youtube_parse_player(const char *json, gchar **audio_url,
                         gchar **title, gchar **artist, gchar **error)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    JsonNode       *root;
    JsonObject     *obj, *details, *streaming, *playability;
    JsonArray      *formats = NULL;
    const char     *status, *reason = NULL;
    Pick            pick = { 0 };

    if (!json_parser_load_from_data(parser, json, -1, NULL)) {
        if (error != NULL)
            *error = g_strdup("返回的不是合法 JSON");
        return -1;
    }

    root = json_parser_get_root(parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
        if (error != NULL)
            *error = g_strdup("返回的不是 JSON 对象");
        return -1;
    }

    obj = json_node_get_object(root);

    playability = obj_object(obj, "playabilityStatus");
    status      = obj_str(playability, "status");

    if (status != NULL && g_strcmp0(status, "OK") != 0) {
        JsonArray  *messages = obj_array(playability, "messages");
        const char *text = NULL;

        reason = obj_str(playability, "reason");

        if (reason == NULL && messages != NULL && json_array_get_length(messages) > 0) {
            JsonNode *first = json_array_get_element(messages, 0);

            if (first != NULL && JSON_NODE_HOLDS_OBJECT(first))
                text = obj_str(json_node_get_object(first), "text");
        }

        if (error != NULL) {
            *error = g_strdup_printf("%s%s%s", status,
                                     (reason != NULL || text != NULL) ? ": " : "",
                                     reason != NULL ? reason : (text != NULL ? text : ""));
        }

        return -1;
    }

    details = obj_object(obj, "videoDetails");

    if (title != NULL) {
        const char *t = obj_str(details, "title");

        if (t != NULL && *t != '\0')
            *title = g_strdup(t);
    }

    if (artist != NULL) {
        const char *a = obj_str(details, "author");

        if (a != NULL && *a != '\0')
            *artist = g_strdup(a);
    }

    streaming = obj_object(obj, "streamingData");
    if (streaming == NULL) {
        if (error != NULL)
            *error = g_strdup("返回里没有 streamingData");
        return -1;
    }

    formats = obj_array(streaming, "adaptiveFormats");
    if (formats != NULL) {
        for (guint i = 0; i < json_array_get_length(formats); i++) {
            JsonNode *node = json_array_get_element(formats, i);

            if (node != NULL && JSON_NODE_HOLDS_OBJECT(node))
                consider(&pick, json_node_get_object(node), TRUE);
        }
    }

    /* 自适应流全是加密的：退一步拿音视频合一的，至少能出声 */
    if (pick.url == NULL) {
        formats = obj_array(streaming, "formats");

        if (formats != NULL) {
            for (guint i = 0; i < json_array_get_length(formats); i++) {
                JsonNode *node = json_array_get_element(formats, i);

                if (node != NULL && JSON_NODE_HOLDS_OBJECT(node))
                    consider(&pick, json_node_get_object(node), FALSE);
            }
        }
    }

    if (pick.url == NULL) {
        if (error != NULL)
            *error = g_strdup("没有可用的明文音频流（只有加密流，换个客户端再试）");
        return -1;
    }

    if (audio_url != NULL) {
        *audio_url = url_without_chunk_params(pick.url);

        g_message("youtube: %s%s @%" G_GINT64_FORMAT " bps",
                  pick.mime != NULL ? pick.mime : "?",
                  pick.throttled ? " (带 n=，会被限速)" : "",
                  pick.bitrate);
    }

    pick_free(&pick);

    return 0;
}

/* ---------- 真正的解析流程 ---------- */

static gchar *player_body(const YtClient *c, const gchar *video_id)
{
    return g_strdup_printf(
        "{\"videoId\":\"%s\","
        "\"context\":{\"client\":{"
        "\"clientName\":\"%s\",\"clientVersion\":\"%s\","
        "\"hl\":\"en\",\"gl\":\"US\""
        "%s}},"
        "\"contentCheckOk\":true,\"racyCheckOk\":true}",
        video_id, c->name, c->version, c->extra);
}

static gchar *player_headers(const YtClient *c)
{
    return g_strdup_printf(
        "Content-Type: application/json\r\n"
        "Accept: */*\r\n"
        "Origin: https://www.youtube.com\r\n"
        "Referer: https://www.youtube.com/\r\n"
        "User-Agent: %s\r\n"
        "X-Youtube-Client-Name: %s\r\n"
        "X-Youtube-Client-Version: %s\r\n",
        c->ua, c->key, c->version);
}

int youtube_resolve(const char *page_url, gchar **audio_url,
                    gchar **title, gchar **artist, gchar **error)
{
    g_autofree gchar *video_id = youtube_video_id(page_url);
    GString  *reasons;
    gboolean  bot_check = FALSE;   /* 所有客户端都撞上人机验证 */
    gchar    *fallback_url = NULL; /* 带 rqh 的保底地址 */
    const gchar *fallback_client = NULL;

    if (audio_url != NULL) *audio_url = NULL;
    if (title != NULL)     *title     = NULL;
    if (artist != NULL)    *artist    = NULL;
    if (error != NULL)     *error     = NULL;

    if (video_id == NULL) {
        if (error != NULL)
            *error = g_strdup("链接里找不到 YouTube 视频 ID");
        return -1;
    }

    reasons = g_string_new(NULL);

    for (int i = 0; i < YT_CLIENT_COUNT; i++) {
        const YtClient *client = &g_clients[i];
        g_autofree gchar *body    = player_body(client, video_id);
        g_autofree gchar *headers = player_headers(client);
        HttpResponse   resp = {0};
        g_autofree gchar *why = NULL;
        gchar *url = NULL;

        if (http_post_with_headers(YT_PLAYER_URL, body, headers, &resp, 12) != 0) {
            /* 传输层就失败（DNS / 连接 / 超时）：所有客户端都是同一个域名，
             * 再试别的只是白等，直接给出可操作的原因。 */
            if (error != NULL)
                *error = g_strdup_printf(
                    "连不上 YouTube（%s）：请在 config.ini 的 [net] proxy 填代理地址",
                    resp.error[0] != '\0' ? resp.error : "传输失败");

            g_debug("youtube: %s 传输失败：%s", video_id, resp.error);

            g_string_free(reasons, TRUE);
            http_response_free(&resp);
            return -1;
        }

        if (resp.status != 200) {
            g_string_append_printf(reasons, "%s=HTTP %ld; ",
                                   client->name, resp.status);
            http_response_free(&resp);
            continue;
        }

        if (youtube_parse_player(resp.body, &url, title, artist, &why) == 0) {
            /* 带 rqh=1 的地址只能取开头约 1MB（后面的 range 一律 403），
             * 播到一半就停、也 seek 不了。先留着当保底，继续试别的客户端，
             * 能拿到不带 rqh 的就用更好的那个。 */
            if (url_has_param(url, "rqh")) {
                g_free(fallback_url);
                fallback_url = url;
                fallback_client = client->name;

                g_string_append_printf(reasons, "%s=只能取开头 1MB; ", client->name);
                http_response_free(&resp);
                continue;
            }

            g_message("youtube: %s -> %s (%s)",
                      video_id, title != NULL ? *title : "?", client->name);

            if (audio_url != NULL)
                *audio_url = url;
            else
                g_free(url);

            g_string_free(reasons, TRUE);
            http_response_free(&resp);
            g_free(fallback_url);
            return 0;
        }

        if (why != NULL && strstr(why, "not a bot") != NULL)
            bot_check = TRUE;

        g_string_append_printf(reasons, "%s=%s; ", client->name,
                               why != NULL ? why : "失败");

        http_response_free(&resp);
    }

    /* 没拿到自由地址，但有带 rqh 的保底地址：用它（只能播开头 1MB 左右） */
    if (fallback_url != NULL) {
        g_message("youtube: %s -> 只有 %s 的地址带 rqh，大约只能播开头 1MB",
                  video_id, fallback_client != NULL ? fallback_client : "?");

        if (audio_url != NULL)
            *audio_url = fallback_url;
        else
            g_free(fallback_url);

        g_string_free(reasons, TRUE);
        return 0;
    }

    g_debug("youtube: %s 全部客户端都失败：%s", video_id,
            reasons->len > 0 ? reasons->str : "(无说明)");

    if (error != NULL) {
        if (bot_check) {
            /* 这不是代码问题：YouTube 按出口 IP 判断，机房 / 公共代理常被这样挡 */
            *error = g_strdup("YouTube 要人机验证：换个代理节点 / IP 再试");
        } else {
            /* 界面上只有一行，只放第一个原因；全量在 debug 日志里 */
            *error = g_strdup_printf("YouTube 解析失败：%s",
                                     reasons->len > 0 ? reasons->str : "没给出音频");
        }
    }

    g_string_free(reasons, TRUE);

    return -1;
}

gchar *youtube_request_headers(void)
{
    return g_strdup_printf("User-Agent: %s\r\nAccept: */*\r\n", config_user_agent());
}
