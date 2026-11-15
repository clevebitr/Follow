/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#include "musicbrainz.h"

#include <string.h>

#include <json-glib/json-glib.h>

#include "config.h"
#include "http.h"

#define MB_API_BASE   "https://musicbrainz.org/ws/2"
#define MB_MIN_GAP_US 1300000          /* 官方限速 1 req/s，保留余量。 */
#define CAA_BASE      "https://coverartarchive.org/release"

static GMutex g_mb_mutex;
static gint64 g_mb_last_us;

static void mb_throttle(void)
{
    gint64 now;

    g_mutex_lock(&g_mb_mutex);

    now = g_get_monotonic_time();

    if (g_mb_last_us != 0) {
        gint64 wait = MB_MIN_GAP_US - (now - g_mb_last_us);

        if (wait > 0)
            g_usleep((gulong)wait);
    }

    g_mb_last_us = g_get_monotonic_time();
    g_mutex_unlock(&g_mb_mutex);
}

static gchar *mb_http_error(const HttpResponse *resp)
{
    if (resp->status == 404)
        return g_strdup("MusicBrainz 上没有这条记录");

    if (resp->status == 503)
        return g_strdup("MusicBrainz 限速（503），请稍后再试");

    if (resp->error[0] != '\0')
        return g_strdup(resp->error);

    if (resp->status != 200)
        return g_strdup_printf("HTTP %ld", resp->status);

    return NULL;
}

/* 同步获取 JSON；parser 由调用方 unref。 */
static gboolean mb_get_json(const gchar *url, JsonParser **out_parser,
                            JsonNode **out_root, gchar **error)
{
    HttpResponse  resp = { 0 };
    JsonParser   *parser;
    g_autofree gchar *headers = NULL;
    g_autofree gchar *err = NULL;
    GError       *json_error = NULL;

    mb_throttle();

    headers = g_strdup_printf("User-Agent: %s\r\n"
                              "Accept: application/json",
                              config_metadata_user_agent());

    if (http_get_with_headers(url, headers, &resp, 20) != 0 ||
        resp.status != 200) {
        err = mb_http_error(&resp);
        if (error != NULL)
            *error = g_strdup(err != NULL ? err : "请求失败");
        http_response_free(&resp);
        return FALSE;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, resp.body, (gssize)resp.length,
                                    &json_error)) {
        if (error != NULL)
            *error = g_strdup_printf("JSON 解析失败：%s",
                                     json_error != NULL ? json_error->message
                                                        : "unknown");
        g_clear_error(&json_error);
        g_object_unref(parser);
        http_response_free(&resp);
        return FALSE;
    }

    http_response_free(&resp);

    *out_parser = parser;
    *out_root   = json_parser_get_root(parser);
    return TRUE;
}

static const gchar *jo_string(JsonObject *obj, const gchar *member)
{
    JsonNode *node;

    if (obj == NULL || !json_object_has_member(obj, member))
        return NULL;

    node = json_object_get_member(obj, member);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE(node))
        return NULL;

    if (json_node_get_value_type(node) != G_TYPE_STRING)
        return NULL;

    return json_node_get_string(node);
}

static gint jo_int(JsonObject *obj, const gchar *member, gint fallback)
{
    JsonNode *node;
    GType     type;

    if (obj == NULL || !json_object_has_member(obj, member))
        return fallback;

    node = json_object_get_member(obj, member);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE(node))
        return fallback;

    type = json_node_get_value_type(node);

    if (type == G_TYPE_INT64)
        return (gint)json_node_get_int(node);

    if (type == G_TYPE_DOUBLE)
        return (gint)json_node_get_double(node);

    if (type == G_TYPE_STRING)
        return atoi(json_node_get_string(node));

    return fallback;
}

/* artist-credit[] 拼接为显示字符串。 */
static gchar *artist_credit_string(JsonArray *credits)
{
    GString *out;
    guint    n;

    if (credits == NULL)
        return NULL;

    out = g_string_new(NULL);
    n   = json_array_get_length(credits);

    for (guint i = 0; i < n; i++) {
        JsonNode   *node = json_array_get_element(credits, i);
        JsonObject *obj  = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
            ? json_node_get_object(node) : NULL;
        const gchar *name;
        const gchar *join;

        if (obj == NULL)
            continue;

        name = jo_string(obj, "name");

        if (name == NULL && json_object_has_member(obj, "artist")) {
            JsonNode   *an = json_object_get_member(obj, "artist");
            JsonObject *ao = an != NULL && JSON_NODE_HOLDS_OBJECT(an)
                ? json_node_get_object(an) : NULL;

            name = jo_string(ao, "name");
        }

        if (name != NULL)
            g_string_append(out, name);

        join = jo_string(obj, "joinphrase");
        if (join != NULL)
            g_string_append(out, join);
    }

    if (out->len == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }

    return g_string_free(out, FALSE);
}

/* 选择代表专辑：Album > EP > Single，优先 Official 与有日期。 */
static JsonObject *pick_release(JsonArray *releases)
{
    JsonObject *best = NULL;
    gint        best_score = -1;

    if (releases == NULL)
        return NULL;

    for (guint i = 0; i < json_array_get_length(releases); i++) {
        JsonNode   *node = json_array_get_element(releases, i);
        JsonObject *rel  = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
            ? json_node_get_object(node) : NULL;
        JsonObject *group = NULL;
        const gchar *status;
        const gchar *type = NULL;
        gint         score = 0;

        if (rel == NULL)
            continue;

        if (json_object_has_member(rel, "release-group")) {
            JsonNode *gn = json_object_get_member(rel, "release-group");

            if (gn != NULL && JSON_NODE_HOLDS_OBJECT(gn))
                group = json_node_get_object(gn);
        }

        if (group != NULL)
            type = jo_string(group, "primary-type");

        if (type != NULL) {
            if (g_ascii_strcasecmp(type, "Album") == 0)       score += 6;
            else if (g_ascii_strcasecmp(type, "EP") == 0)     score += 4;
            else if (g_ascii_strcasecmp(type, "Single") == 0) score += 2;
        }

        status = jo_string(rel, "status");
        if (status != NULL && g_ascii_strcasecmp(status, "Official") == 0)
            score += 3;

        if (jo_string(rel, "date") != NULL)
            score += 2;

        if (score > best_score) {
            best_score = score;
            best       = rel;
        }
    }

    return best;
}

static JsonArray *jo_array(JsonObject *obj, const gchar *member)
{
    JsonNode *node;

    if (obj == NULL || !json_object_has_member(obj, member))
        return NULL;

    node = json_object_get_member(obj, member);

    if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
        return NULL;

    return json_node_get_array(node);
}

static gchar *tags_string(JsonArray *tags)
{
    GString *out;
    guint    n;

    if (tags == NULL || json_array_get_length(tags) == 0)
        return NULL;

    out = g_string_new(NULL);
    n   = MIN(json_array_get_length(tags), 8u);

    for (guint i = 0; i < n; i++) {
        JsonNode   *node = json_array_get_element(tags, i);
        JsonObject *obj  = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
            ? json_node_get_object(node) : NULL;
        const gchar *name = jo_string(obj, "name");

        if (name == NULL || *name == '\0')
            continue;

        if (out->len > 0)
            g_string_append(out, ", ");

        g_string_append(out, name);
    }

    if (out->len == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }

    return g_string_free(out, FALSE);
}

GPtrArray *mb_search_recordings(const gchar *title, const gchar *artist,
                                gchar **error)
{
    JsonParser  *parser = NULL;
    JsonNode    *root   = NULL;
    JsonArray   *recs;
    GPtrArray   *out;
    g_autofree gchar *query   = NULL;
    g_autofree gchar *escaped = NULL;
    g_autofree gchar *url     = NULL;
    g_autofree gchar *err     = NULL;

    if (error != NULL)
        *error = NULL;

    if (title == NULL || *title == '\0') {
        if (error != NULL)
            *error = g_strdup("曲名是空的，没法查 MusicBrainz");
        return NULL;
    }

    if (artist != NULL && *artist != '\0')
        query = g_strdup_printf("recording:\"%s\" AND artist:\"%s\"",
                                title, artist);
    else
        query = g_strdup_printf("recording:\"%s\"", title);

    escaped = g_uri_escape_string(query, NULL, TRUE);
    url = g_strdup_printf(MB_API_BASE "/recording?fmt=json&limit=10&query=%s",
                          escaped);

    if (!mb_get_json(url, &parser, &root, &err)) {
        if (error != NULL)
            *error = g_strdup(err != NULL ? err : "搜索失败");
        return NULL;
    }

    out = g_ptr_array_new_with_free_func(mb_candidate_free);

    {
        JsonObject *obj = root != NULL && JSON_NODE_HOLDS_OBJECT(root)
            ? json_node_get_object(root) : NULL;
        JsonNode   *rn = obj != NULL ? json_object_get_member(obj, "recordings")
                                     : NULL;

        recs = rn != NULL && JSON_NODE_HOLDS_ARRAY(rn)
            ? json_node_get_array(rn) : NULL;
    }

    for (guint i = 0; recs != NULL && i < json_array_get_length(recs); i++) {
        JsonNode   *node = json_array_get_element(recs, i);
        JsonObject *rec  = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
            ? json_node_get_object(node) : NULL;
        JsonObject *rel;
        JsonArray  *credits;
        JsonArray  *releases;
        MbCandidate *cand;

        if (rec == NULL)
            continue;

        cand = g_new0(MbCandidate, 1);
        cand->mbid = g_strdup(jo_string(rec, "id"));
        cand->title = g_strdup(jo_string(rec, "title"));
        cand->score = jo_int(rec, "score", 0);

        credits = jo_array(rec, "artist-credit");
        cand->artist = artist_credit_string(credits);

        releases = jo_array(rec, "releases");
        rel = pick_release(releases);

        if (rel != NULL) {
            cand->album      = g_strdup(jo_string(rel, "title"));
            cand->album_mbid = g_strdup(jo_string(rel, "id"));
            cand->date       = g_strdup(jo_string(rel, "date"));
        }

        if (cand->mbid == NULL || cand->title == NULL) {
            mb_candidate_free(cand);
            continue;
        }

        g_ptr_array_add(out, cand);
    }

    g_object_unref(parser);

    if (out->len == 0) {
        g_ptr_array_unref(out);
        if (error != NULL)
            *error = g_strdup("没有找到匹配的录音");
        return NULL;
    }

    return out;
}

/* 填充曲序、总轨数、厂牌、条码。 */
static void release_fill(MbRecord *record, JsonObject *rel)
{
    JsonArray *media;
    JsonArray *labels;
    gint       number = 0;
    gint       count  = 0;

    record->album = g_strdup(jo_string(rel, "title"));
    record->release_date = g_strdup(jo_string(rel, "date"));
    record->country = g_strdup(jo_string(rel, "country"));
    record->barcode = g_strdup(jo_string(rel, "barcode"));

    labels = jo_array(rel, "label-info");

    if (labels != NULL && json_array_get_length(labels) > 0) {
        JsonNode   *node = json_array_get_element(labels, 0);
        JsonObject *info = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
            ? json_node_get_object(node) : NULL;

        record->catalog_number = g_strdup(jo_string(info, "catalog-number"));

        if (info != NULL && json_object_has_member(info, "label")) {
            JsonNode   *ln = json_object_get_member(info, "label");
            JsonObject *lo = ln != NULL && JSON_NODE_HOLDS_OBJECT(ln)
                ? json_node_get_object(ln) : NULL;

            record->label = g_strdup(jo_string(lo, "name"));
        }
    }

    record->album_artist = artist_credit_string(jo_array(rel, "artist-credit"));

    media = jo_array(rel, "media");

    for (guint m = 0; media != NULL && m < json_array_get_length(media); m++) {
        JsonNode   *mnode = json_array_get_element(media, m);
        JsonObject *mo    = mnode != NULL && JSON_NODE_HOLDS_OBJECT(mnode)
            ? json_node_get_object(mnode) : NULL;
        JsonArray  *tracks;
        gint        delta = count;

        if (mo == NULL)
            continue;

        count += jo_int(mo, "track-count", 0);

        if (number > 0)
            continue;

        tracks = jo_array(mo, "tracks");

        for (guint t = 0; tracks != NULL && t < json_array_get_length(tracks); t++) {
            JsonNode   *tnode = json_array_get_element(tracks, t);
            JsonObject *to    = tnode != NULL && JSON_NODE_HOLDS_OBJECT(tnode)
                ? json_node_get_object(tnode) : NULL;
            const gchar *rid = NULL;

            if (to == NULL)
                continue;

            if (json_object_has_member(to, "recording")) {
                JsonNode   *rn = json_object_get_member(to, "recording");
                JsonObject *ro = rn != NULL && JSON_NODE_HOLDS_OBJECT(rn)
                    ? json_node_get_object(rn) : NULL;

                rid = jo_string(ro, "id");
            }

            if (rid != NULL && g_strcmp0(rid, record->mbid) == 0) {
                gint pos = jo_int(to, "position", 0);

                number = delta + (pos > 0 ? pos : (gint)t + 1);
                break;
            }
        }
    }

    record->track_number = number;
    record->track_count  = count;
}

MbRecord *mb_fetch_recording(const gchar *mbid, gchar **error)
{
    JsonParser  *parser = NULL;
    JsonNode    *root   = NULL;
    JsonObject  *rec;
    JsonObject  *rel   = NULL;
    JsonArray   *releases;
    MbRecord    *record;
    g_autofree gchar *url = NULL;
    g_autofree gchar *err = NULL;
    gchar       *release_mbid = NULL;

    if (error != NULL)
        *error = NULL;

    if (mbid == NULL || *mbid == '\0') {
        if (error != NULL)
            *error = g_strdup("MBID 是空的");
        return NULL;
    }

    url = g_strdup_printf(MB_API_BASE
                          "/recording/%s?fmt=json&inc=artist-credits+releases+release-groups+tags",
                          mbid);

    if (!mb_get_json(url, &parser, &root, &err)) {
        if (error != NULL)
            *error = g_strdup(err != NULL ? err : "查询录音失败");
        return NULL;
    }

    rec = root != NULL && JSON_NODE_HOLDS_OBJECT(root)
        ? json_node_get_object(root) : NULL;

    if (rec == NULL) {
        g_object_unref(parser);
        if (error != NULL)
            *error = g_strdup("MusicBrainz 返回了空对象");
        return NULL;
    }

    record = g_new0(MbRecord, 1);
    record->mbid        = g_strdup(mbid);
    record->title       = g_strdup(jo_string(rec, "title"));
    record->duration_ms = jo_int(rec, "length", 0);

    record->artist = artist_credit_string(jo_array(rec, "artist-credit"));
    record->tags   = tags_string(jo_array(rec, "tags"));

    releases = jo_array(rec, "releases");
    rel = pick_release(releases);

    if (rel != NULL) {
        release_mbid = g_strdup(jo_string(rel, "id"));

        /* releases[] 字段不完整，补拉 release 详情。 */
        if (release_mbid != NULL) {
            JsonParser *release_parser = NULL;
            JsonNode   *release_root   = NULL;
            g_autofree gchar *release_url = g_strdup_printf(
                MB_API_BASE "/release/%s?fmt=json&inc=labels+artist-credits+recordings",
                release_mbid);
            g_autofree gchar *release_err = NULL;

            if (mb_get_json(release_url, &release_parser, &release_root,
                            &release_err)) {
                JsonObject *release = release_root != NULL &&
                                      JSON_NODE_HOLDS_OBJECT(release_root)
                    ? json_node_get_object(release_root) : NULL;

                if (release != NULL)
                    release_fill(record, release);

                g_object_unref(release_parser);
            } else {
                /* release 详情失败时保留已有字段。 */
                g_debug("musicbrainz: release %s detail failed: %s",
                        release_mbid, release_err != NULL ? release_err : "-");
            }

            record->album_mbid = g_strdup(release_mbid);
            record->cover_url  = mb_cover_url(release_mbid);
        }

        if (record->album == NULL)
            record->album = g_strdup(jo_string(rel, "title"));

        if (record->release_date == NULL)
            record->release_date = g_strdup(jo_string(rel, "date"));
    }

    g_object_unref(parser);

    if (record->title == NULL)
        record->title = g_strdup(mbid);

    return record;
}

gchar *mb_cover_url(const gchar *release_mbid)
{
    if (release_mbid == NULL || *release_mbid == '\0')
        return NULL;

    return g_strdup_printf(CAA_BASE "/%s/front-500", release_mbid);
}

void mb_candidate_free(gpointer data)
{
    MbCandidate *cand = data;

    if (cand == NULL)
        return;

    g_free(cand->mbid);
    g_free(cand->title);
    g_free(cand->artist);
    g_free(cand->album);
    g_free(cand->album_mbid);
    g_free(cand->date);
    g_free(cand);
}

void mb_record_free(MbRecord *record)
{
    if (record == NULL)
        return;

    g_free(record->mbid);
    g_free(record->title);
    g_free(record->artist);
    g_free(record->album);
    g_free(record->album_mbid);
    g_free(record->album_artist);
    g_free(record->release_date);
    g_free(record->country);
    g_free(record->label);
    g_free(record->catalog_number);
    g_free(record->barcode);
    g_free(record->tags);
    g_free(record->cover_url);
    g_free(record);
}
