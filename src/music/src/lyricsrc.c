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

#include "http.h"
#include "lyricsrc.h"

#define NETEASE_REFERER "Referer: https://music.163.com/\r\n"
#define QQ_REFERER      "Referer: https://y.qq.com/\r\n"

/* 与 bilibili.c 同理：网易云 / QQ 音乐的搜索接口同样是网页端私有接口，
 * 只用来取回用户本来就能看到的歌词文本，不触碰音频流；仅供个人学习使用。 */


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

static JsonObject *parse_object(const char *body)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    JsonNode             *root;

    if (body == NULL)
        return NULL;

    if (!json_parser_load_from_data(parser, body, -1, NULL))
        return NULL;

    root = json_parser_get_root(parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
        return NULL;

    return json_object_ref(json_node_get_object(root));
}

static gchar *http_get_utf8(const gchar *url, const gchar *headers, gchar **error)
{
    HttpResponse resp = {0};
    gchar       *body = NULL;

    if (http_get_with_headers(url, headers, &resp, 15) != 0 || resp.status != 200) {
        if (error != NULL)
            *error = g_strdup_printf("HTTP %ld %s", resp.status, resp.error);
        http_response_free(&resp);
        return NULL;
    }

    body = g_strdup(resp.body != NULL ? resp.body : "");
    http_response_free(&resp);

    return body;
}

/* QQ 的歌词是 HTML 转义的（&#10; &#58; &amp; ...） */
static gchar *html_unescape(const gchar *text)
{
    GString *out = g_string_sized_new(strlen(text) + 64);
    const gchar *p = text;

    while (*p != '\0') {
        if (*p == '&') {
            if (g_str_has_prefix(p, "&#")) {
                const gchar *semi = strchr(p, ';');

                if (semi != NULL) {
                    gint64 code = g_ascii_strtoll(p + 2, NULL, 10);

                    if (code > 0 && code < 0x110000) {
                        g_string_append_unichar(out, (gunichar)code);
                        p = semi + 1;
                        continue;
                    }
                }
            } else if (g_str_has_prefix(p, "&amp;")) {
                g_string_append_c(out, '&'); p += 5; continue;
            } else if (g_str_has_prefix(p, "&lt;")) {
                g_string_append_c(out, '<'); p += 4; continue;
            } else if (g_str_has_prefix(p, "&gt;")) {
                g_string_append_c(out, '>'); p += 4; continue;
            } else if (g_str_has_prefix(p, "&quot;")) {
                g_string_append_c(out, '"'); p += 6; continue;
            } else if (g_str_has_prefix(p, "&apos;")) {
                g_string_append_c(out, '\''); p += 6; continue;
            }
        }

        g_string_append_c(out, *p++);
    }

    return g_string_free(out, FALSE);
}

/* 网易云的逐字歌词（yrc）：[行起始,行时长](词起始,词时长,0)字... */
static gboolean looks_like_yrc(const gchar *text)
{
    const gchar *p = text;

    while (p != NULL && *p != (gchar)0) {
        if (*p == '[' && g_ascii_isdigit(p[1])) {
            const gchar *br    = strchr(p, ']');
            const gchar *comma = strchr(p, ',');

            if (br != NULL && comma != NULL && comma < br &&
                br[1] == '(' && g_ascii_isdigit(br[2]))
                return TRUE;
        }

        p = strchr(p, (gchar)10);
        if (p != NULL)
            p++;
    }

    return FALSE;
}

/* 判断一份 LRC 存不存在时间轴 */
static gboolean lrc_has_timing(const gchar *lrc)
{
    gint count = 0;
    const gchar *p = lrc;

    while ((p = strstr(p, "[")) != NULL) {
        if (g_ascii_isdigit(p[1]) && g_ascii_isdigit(p[2]) && p[3] == ':') {
            if (++count >= 3)
                return TRUE;
        }
        p++;
    }

    return FALSE;
}

/* 网易云 */

static gchar *netease_search(const gchar *query, gchar **matched, gchar **error)
{
    g_autofree gchar *escaped = NULL;
    g_autofree gchar *url = NULL;
    g_autofree gchar *body = NULL;
    JsonObject       *root;
    JsonObject       *result;
    JsonArray        *songs;
    gchar            *id = NULL;

    escaped = g_uri_escape_string(query, NULL, FALSE);
    url = g_strdup_printf("https://music.163.com/api/search/get"
                          "?s=%s&type=1&limit=5&offset=0", escaped);

    body = http_get_utf8(url, NETEASE_REFERER, error);
    if (body == NULL)
        return NULL;

    root = parse_object(body);
    if (root == NULL) {
        if (error != NULL) *error = g_strdup("网易云：返回的不是 JSON");
        return NULL;
    }

    result = json_object_get_object_member(root, "result");
    songs  = (result != NULL && json_object_has_member(result, "songs"))
             ? json_object_get_array_member(result, "songs") : NULL;

    if (songs == NULL || json_array_get_length(songs) == 0) {
        if (error != NULL) *error = g_strdup_printf("网易云：搜不到「%s」", query);
        json_object_unref(root);
        return NULL;
    }

    {
        JsonNode   *first = json_array_get_element(songs, 0);
        JsonObject *song = first != NULL && JSON_NODE_HOLDS_OBJECT(first)
                           ? json_node_get_object(first) : NULL;

        if (song == NULL) {
            if (error != NULL) *error = g_strdup("网易云：搜索结果格式不对");
            json_object_unref(root);
            return NULL;
        }

        id = g_strdup_printf("%" G_GINT64_FORMAT, obj_int(song, "id", 0));

        if (matched != NULL) {
            JsonArray  *artists = json_object_has_member(song, "artists")
                                   ? json_object_get_array_member(song, "artists") : NULL;
            const char *name    = obj_str(song, "name");
            const char *artist  = NULL;

            if (artists != NULL && json_array_get_length(artists) > 0) {
                JsonNode *a = json_array_get_element(artists, 0);

                if (a != NULL && JSON_NODE_HOLDS_OBJECT(a))
                    artist = obj_str(json_node_get_object(a), "name");
            }

            *matched = g_strdup_printf("%s - %s", artist != NULL ? artist : "?",
                                       name != NULL ? name : "?");
        }
    }

    json_object_unref(root);
    return id;
}

static gchar *netease_lyric(const gchar *id, gchar **error)
{
    g_autofree gchar *url = NULL;
    g_autofree gchar *body = NULL;
    JsonObject       *root;
    JsonObject       *lrc;
    const char       *text;
    gchar            *out = NULL;

    url  = g_strdup_printf("https://music.163.com/api/song/lyric"
                           "?os=pc&id=%s&lv=-1&kv=-1&tv=-1&yv=-1", id);
    body = http_get_utf8(url, NETEASE_REFERER, error);
    if (body == NULL)
        return NULL;

    root = parse_object(body);
    if (root == NULL) {
        if (error != NULL) *error = g_strdup("网易云：歌词返回的不是 JSON");
        return NULL;
    }

    /* 优先用逐字歌词（yrc）：有它才能做逐字高亮，没有就退回整行 LRC */
    {
        JsonObject *yrc = json_object_has_member(root, "yrc")
                          ? json_object_get_object_member(root, "yrc") : NULL;
        const char *ytext = obj_str(yrc, "lyric");

        if (ytext != NULL && looks_like_yrc(ytext))
            out = g_strdup(ytext);
    }

    lrc  = json_object_has_member(root, "lrc")
           ? json_object_get_object_member(root, "lrc") : NULL;
    text = obj_str(lrc, "lyric");

    if (out != NULL) {
        /* 拿到逐字歌词了 */
    } else if (text == NULL || !lrc_has_timing(text)) {
        if (error != NULL) *error = g_strdup("网易云：这首歌没有带时间轴的歌词");
    } else {
        out = g_strdup(text);
    }

    json_object_unref(root);
    return out;
}

/* QQ 音乐 */

static gchar *qq_search(const gchar *query, gchar **matched, gchar **error)
{
    g_autofree gchar *escaped = NULL;
    g_autofree gchar *url = NULL;
    g_autofree gchar *body = NULL;
    JsonObject       *root;
    JsonObject       *data, *song;
    JsonArray        *list;
    gchar            *mid = NULL;

    escaped = g_uri_escape_string(query, NULL, FALSE);
    url = g_strdup_printf("https://c.y.qq.com/soso/fcgi-bin/client_search_cp"
                          "?w=%s&format=json&n=5&p=1&aggr=1&cr=1&flag_qc=0", escaped);

    body = http_get_utf8(url, QQ_REFERER, error);
    if (body == NULL)
        return NULL;

    root = parse_object(body);
    if (root == NULL) {
        if (error != NULL) *error = g_strdup("QQ 音乐：返回的不是 JSON");
        return NULL;
    }

    data = json_object_get_object_member(root, "data");
    song = data != NULL ? json_object_get_object_member(data, "song") : NULL;
    list = (song != NULL && json_object_has_member(song, "list"))
           ? json_object_get_array_member(song, "list") : NULL;

    if (list == NULL || json_array_get_length(list) == 0) {
        if (error != NULL) *error = g_strdup_printf("QQ 音乐：搜不到「%s」", query);
        json_object_unref(root);
        return NULL;
    }

    {
        JsonNode   *first = json_array_get_element(list, 0);
        JsonObject *item = first != NULL && JSON_NODE_HOLDS_OBJECT(first)
                           ? json_node_get_object(first) : NULL;
        const char *mid_str = obj_str(item, "songmid");

        if (mid_str == NULL) {
            if (error != NULL) *error = g_strdup("QQ 音乐：搜索结果格式不对");
            json_object_unref(root);
            return NULL;
        }

        mid = g_strdup(mid_str);

        if (matched != NULL) {
            JsonArray  *singers = json_object_has_member(item, "singer")
                                   ? json_object_get_array_member(item, "singer") : NULL;
            const char *name    = obj_str(item, "songname");
            const char *artist  = NULL;

            if (singers != NULL && json_array_get_length(singers) > 0) {
                JsonNode *s = json_array_get_element(singers, 0);

                if (s != NULL && JSON_NODE_HOLDS_OBJECT(s))
                    artist = obj_str(json_node_get_object(s), "name");
            }

            *matched = g_strdup_printf("%s - %s", artist != NULL ? artist : "?",
                                       name != NULL ? name : "?");
        }
    }

    json_object_unref(root);
    return mid;
}

static gchar *qq_lyric(const gchar *mid, gchar **error)
{
    g_autofree gchar *url = NULL;
    g_autofree gchar *body = NULL;
    g_autofree gchar *unescaped = NULL;
    JsonObject       *root;
    const char       *text;
    gchar            *out = NULL;

    url  = g_strdup_printf("https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg"
                           "?songmid=%s&format=json&nobase64=1&g_tk=5381", mid);
    body = http_get_utf8(url, QQ_REFERER, error);
    if (body == NULL)
        return NULL;

    root = parse_object(body);
    if (root == NULL) {
        if (error != NULL) *error = g_strdup("QQ 音乐：歌词返回的不是 JSON");
        return NULL;
    }

    text = obj_str(root, "lyric");
    if (text == NULL || text[0] == '\0') {
        if (error != NULL) *error = g_strdup("QQ 音乐：这首歌没有歌词");
        json_object_unref(root);
        return NULL;
    }

    unescaped = html_unescape(text);

    if (!lrc_has_timing(unescaped)) {
        if (error != NULL) *error = g_strdup("QQ 音乐：歌词没有时间轴");
    } else {
        out = g_strdup(unescaped);
    }

    json_object_unref(root);
    return out;
}

/* 对外接口 */

gboolean lyricsrc_parse(const gchar *spec, gchar **provider, gchar **rest)
{
    const gchar *colon;

    if (spec == NULL)
        return FALSE;

    colon = strchr(spec, ':');

    if (colon == NULL)
        return FALSE;

    {
        g_autofree gchar *name = g_strndup(spec, (gsize)(colon - spec));

        if (g_ascii_strcasecmp(name, "netease") != 0 &&
            g_ascii_strcasecmp(name, "163") != 0 &&
            g_ascii_strcasecmp(name, "qq") != 0 &&
            g_ascii_strcasecmp(name, "qqmusic") != 0)
            return FALSE;

        *provider = g_ascii_strdown(name, -1);
        *rest     = g_strdup(colon + 1);
        return TRUE;
    }
}

gchar *lyricsrc_fetch(const LyricsQuery *query, gchar **error)
{
    g_autofree gchar *search = NULL;
    gchar            *id = NULL;
    gchar            *result = NULL;
    g_autofree gchar *matched = NULL;

    if (error != NULL)
        *error = NULL;

    if (query == NULL || query->provider == NULL) {
        if (error != NULL) *error = g_strdup("没有指定歌词源");
        return NULL;
    }

    /* 关键词：spec 里写了就用它，否则用 "歌手 歌名" */
    if (query->spec != NULL && query->spec[0] != '\0')
        search = g_strdup(query->spec);
    else if (query->artist != NULL && query->title != NULL)
        search = g_strdup_printf("%s %s", query->artist, query->title);
    else if (query->title != NULL)
        search = g_strdup(query->title);
    else {
        if (error != NULL) *error = g_strdup("没有歌名/歌手，也没有 id");
        return NULL;
    }

    if (g_ascii_strcasecmp(query->provider, "qq") == 0 ||
        g_ascii_strcasecmp(query->provider, "qqmusic") == 0) {
        /* QQ 的 songmid 是 14 位字母数字，全数字/带空格的一律当关键词 */
        gboolean looks_like_mid = query->spec != NULL &&
                                  query->spec[0] != 0 &&
                                  strchr(query->spec, ' ') == NULL &&
                                  strlen(query->spec) >= 10;

        if (looks_like_mid) {
            id = g_strdup(query->spec);
        } else {
            id = qq_search(search, &matched, error);
        }

        if (id != NULL)
            result = qq_lyric(id, error);
    } else {
        /* 网易云：纯数字就是歌曲 id，否则按关键词搜索 */
        gboolean looks_like_id = query->spec != NULL &&
                                 query->spec[0] != 0 &&
                                 strspn(query->spec, "0123456789") == strlen(query->spec);

        if (looks_like_id) {
            id = g_strdup(query->spec);
        } else {
            id = netease_search(search, &matched, error);
        }

        if (id != NULL)
            result = netease_lyric(id, error);
    }

    if (result != NULL && matched != NULL)
        g_message("lyricsrc: %s → %s", query->provider, matched);

    g_free(id);
    return result;
}
