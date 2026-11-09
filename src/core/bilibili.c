#include <string.h>

#include <json-glib/json-glib.h>

#include "bilibili.h"
#include "cache.h"
#include "http.h"

#define BILI_REFERER    "https://www.bilibili.com/"
#define BILI_PLAY_HEADERS \
    "Referer: " BILI_REFERER "\r\n" \
    "Origin: https://www.bilibili.com\r\n" \
    "Accept: */*" "\r\n"

gchar *bilibili_request_headers(void)
{
    return g_strdup(BILI_PLAY_HEADERS);
}

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

static gboolean is_bv_char(char c)
{
    return g_ascii_isalnum(c);
}

/* 从链接里抠出 BV 号（BV + 10 位）；没有就返回 NULL */
static gchar *extract_bvid(const char *url)
{
    const char *p = url;

    while ((p = strstr(p, "BV")) != NULL) {
        gboolean ok = TRUE;

        for (int i = 2; i < 12; i++) {
            if (!is_bv_char(p[i])) {
                ok = FALSE;
                break;
            }
        }

        if (ok)
            return g_strndup(p, 12);

        p += 2;
    }

    return NULL;
}

/* 从链接里抠出 av 号（数字部分）；没有就返回 NULL */
static gchar *extract_aid(const char *url)
{
    const char *p = url;

    while ((p = strstr(p, "av")) != NULL) {
        if (g_ascii_isdigit(p[2])) {
            const char *start = p + 2;
            const char *end = start;

            while (g_ascii_isdigit(*end))
                end++;

            return g_strndup(start, (gsize)(end - start));
        }

        p += 2;
    }

    return NULL;
}

static JsonObject *parse_json_object(const char *body, GError **error)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    JsonNode             *root;

    if (!json_parser_load_from_data(parser, body, -1, error))
        return NULL;

    root = json_parser_get_root(parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
        return NULL;

    /* 注意：返回的对象由 parser 持有，这里拷一份深拷贝更省心 */
    return json_object_ref(json_node_get_object(root));
}

/* 把 B 站 API 的 {"code":..,"message":..,"data":..} 解出 data */
static JsonObject *bili_call(const char *url, gchar **error)
{
    HttpResponse   resp = {0};
    JsonObject    *root = NULL;
    JsonObject    *data;
    gint64         code;

    if (http_get_with_headers(url, "Referer: " BILI_REFERER "\r\n", &resp, 15) != 0) {
        *error = g_strdup_printf("请求失败: %s", resp.error);
        http_response_free(&resp);
        return NULL;
    }

    if (resp.status != 200) {
        *error = g_strdup_printf("HTTP %ld", resp.status);
        http_response_free(&resp);
        return NULL;
    }

    root = parse_json_object(resp.body, NULL);
    if (root == NULL) {
        *error = g_strdup("返回的不是合法 JSON");
        http_response_free(&resp);
        return NULL;
    }

    code = obj_int(root, "code", -1);
    if (code != 0) {
        const char *message = obj_str(root, "message");

        *error = g_strdup_printf("B 站接口返回 %" G_GINT64_FORMAT ": %s",
                                 code, message != NULL ? message : "(无说明)");
        json_object_unref(root);
        http_response_free(&resp);
        return NULL;
    }

    data = json_object_get_object_member(root, "data");
    if (data == NULL) {
        *error = g_strdup("返回里没有 data");
        json_object_unref(root);
        http_response_free(&resp);
        return NULL;
    }

    /* root 只是用来拿 data，这里把 data 深拷出来，然后释放 root */
    {
        JsonObject *copy = json_object_ref(data);

        json_object_unref(root);
        http_response_free(&resp);

        return copy;
    }
}

/* ---------- view 元数据缓存（bvid → cid/title/artist）---------- */

static gchar *view_cache_path(const gchar *bvid)
{
    return follow_cache_file("bili-view", bvid, "txt");
}

static gboolean view_cache_load(const gchar *bvid, gint64 *cid,
                                gchar **title, gchar **artist)
{
    g_autofree gchar *path = view_cache_path(bvid);
    g_autofree gchar *content = NULL;
    gchar           **lines;

    if (!g_file_get_contents(path, &content, NULL, NULL))
        return FALSE;

    lines = g_strsplit(content, "\n", 3);

    if (lines[0] == NULL || lines[1] == NULL) {
        g_strfreev(lines);
        return FALSE;
    }

    *cid = g_ascii_strtoll(lines[0], NULL, 10);

    if (title != NULL && *title == NULL && lines[1][0] != 0)
        *title = g_strdup(g_strstrip(lines[1]));

    if (artist != NULL && *artist == NULL) {
        const gchar *a = lines[2] != NULL ? g_strstrip(lines[2]) : "";

        if (a[0] != 0)
            *artist = g_strdup(a);
    }

    g_strfreev(lines);

    return *cid > 0;
}

static void view_cache_save(const gchar *bvid, gint64 cid,
                            const gchar *title, const gchar *artist)
{
    g_autofree gchar *path = view_cache_path(bvid);
    g_autofree gchar *body = g_strdup_printf("%" G_GINT64_FORMAT "\n%s\n%s\n",
                                             cid,
                                             title != NULL ? title : "",
                                             artist != NULL ? artist : "");

    g_file_set_contents(path, body, -1, NULL);
}

/* ---------- 主流程 ---------- */

int bilibili_resolve(const char *page_url, gchar **audio_url,
                     gchar **title, gchar **artist, gchar **error)
{
    g_autofree gchar *bvid = NULL;
    g_autofree gchar *aid = NULL;
    g_autofree gchar *url = g_strdup(page_url);
    g_autofree gchar *view_url = NULL;
    g_autofree gchar *play_url = NULL;
    JsonObject       *view = NULL;
    JsonObject       *play = NULL;
    JsonObject       *dash;
    JsonArray        *audios;
    gint64            cid = 0;
    gint64            best_bandwidth = -1;
    const char       *best_url = NULL;

    *audio_url = NULL;
    if (title != NULL)
        *title = NULL;
    if (artist != NULL)
        *artist = NULL;
    if (error != NULL)
        *error = NULL;

    /* 短链先跟一次重定向，拿到真正的 /video/BVxxx 地址 */
    if (strstr(url, "b23.tv") != NULL || strstr(url, "bili2233.cn") != NULL) {
        HttpResponse resp = {0};

        if (http_get_with_headers(url, "Referer: " BILI_REFERER "\r\n", &resp, 15) == 0 &&
            resp.final_url != NULL) {
            g_free(url);
            url = g_strdup(resp.final_url);
        }

        http_response_free(&resp);
    }

    bvid = extract_bvid(url);
    aid  = extract_aid(url);

    if (bvid == NULL && aid == NULL) {
        if (error != NULL)
            *error = g_strdup("链接里找不到 BV 号 / av 号");
        return -1;
    }

    /* 1) 取 cid / 标题 / UP 主（先用缓存，避免每次播放都打接口） */
    if (bvid != NULL && view_cache_load(bvid, &cid, title, artist)) {
        g_message("bilibili: metadata cache hit (%s)", bvid);
        goto have_cid;
    }

    view_url = bvid != NULL
        ? g_strdup_printf("https://api.bilibili.com/x/web-interface/view?bvid=%s", bvid)
        : g_strdup_printf("https://api.bilibili.com/x/web-interface/view?aid=%s", aid);

    view = bili_call(view_url, error);
    if (view == NULL)
        return -1;

    cid = obj_int(view, "cid", 0);

    if (cid == 0) {
        /* 分 P 视频：data.cid 也存在，但保险起见看 pages[0] */
        JsonArray *pages = json_object_get_array_member(view, "pages");

        if (pages != NULL && json_array_get_length(pages) > 0) {
            JsonNode *first = json_array_get_element(pages, 0);

            if (first != NULL && JSON_NODE_HOLDS_OBJECT(first))
                cid = obj_int(json_node_get_object(first), "cid", 0);
        }
    }

    if (title != NULL) {
        const char *t = obj_str(view, "title");

        if (t != NULL)
            *title = g_strdup(t);
    }

    if (artist != NULL) {
        JsonObject *owner = json_object_get_object_member(view, "owner");
        const char *name = obj_str(owner, "name");

        if (name != NULL)
            *artist = g_strdup(name);
    }

    if (cid == 0) {
        if (error != NULL)
            *error = g_strdup("返回里没有 cid");
        json_object_unref(view);
        return -1;
    }

    if (bvid != NULL)
        view_cache_save(bvid, cid,
                        title != NULL ? *title : NULL,
                        artist != NULL ? *artist : NULL);

    json_object_unref(view);

have_cid:
    if (cid == 0) {
        if (error != NULL)
            *error = g_strdup("没有 cid");
        return -1;
    }

    /* 2) 取 DASH 音轨里码率最高的一条 */
    play_url = bvid != NULL
        ? g_strdup_printf("https://api.bilibili.com/x/player/playurl"
                          "?bvid=%s&cid=%" G_GINT64_FORMAT "&fnval=16&fourk=1", bvid, cid)
        : g_strdup_printf("https://api.bilibili.com/x/player/playurl"
                          "?avid=%s&cid=%" G_GINT64_FORMAT "&fnval=16&fourk=1", aid, cid);

    play = bili_call(play_url, error);
    if (play == NULL)
        return -1;

    dash = json_object_get_object_member(play, "dash");

    if (dash != NULL) {
        audios = json_object_get_array_member(dash, "audio");

        if (audios != NULL) {
            for (guint i = 0; i < json_array_get_length(audios); i++) {
                JsonNode   *node = json_array_get_element(audios, i);
                JsonObject *item;
                const char *candidate;
                gint64      bandwidth;

                if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node))
                    continue;

                item = json_node_get_object(node);
                candidate = obj_str(item, "baseUrl");
                if (candidate == NULL)
                    candidate = obj_str(item, "base_url");
                if (candidate == NULL)
                    continue;

                bandwidth = obj_int(item, "bandwidth", 0);

                if (bandwidth > best_bandwidth) {
                    best_bandwidth = bandwidth;
                    best_url = candidate;
                }
            }
        }
    }

    /* 没有 DASH 就退回 durl（老接口，通常是整段 mp4 的地址） */
    if (best_url == NULL) {
        JsonArray *durl = json_object_get_array_member(play, "durl");

        if (durl != NULL && json_array_get_length(durl) > 0) {
            JsonNode *first = json_array_get_element(durl, 0);

            if (first != NULL && JSON_NODE_HOLDS_OBJECT(first))
                best_url = obj_str(json_node_get_object(first), "url");
        }
    }

    if (best_url == NULL) {
        if (error != NULL)
            *error = g_strdup("接口没给出可用的音频地址（可能是付费/会员/地区限制）");
        json_object_unref(play);
        return -1;
    }

    *audio_url = g_strdup(best_url);

    json_object_unref(play);

    return 0;
}
