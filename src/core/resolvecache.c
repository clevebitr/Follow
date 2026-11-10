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

#include "resolvecache.h"

#include <glib/gstdio.h>

#include "cache.h"
#include "config.h"

static gchar *resolve_path(const gchar *uri)
{
    return follow_cache_file("resolve", uri, "txt");
}

static gint64 parse_int64(const gchar *raw)
{
    gchar *end = NULL;
    gint64 value;

    if (raw == NULL || *raw == '\0')
        return 0;

    value = g_ascii_strtoll(raw, &end, 10);

    if (end == raw)
        return 0;

    return value;
}

gboolean resolvecache_load(const gchar *uri, MediaSource *out)
{
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autofree gchar *path = resolve_path(uri);
    g_autofree gchar *time_raw = NULL;
    gint              ttl  = config_resolve_ttl();
    gint64            then, now;

    if (out == NULL || uri == NULL || *uri == '\0' || ttl <= 0)
        return FALSE;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
        return FALSE;

    time_raw = g_key_file_get_string(kf, "resolve", "time", NULL);
    then     = parse_int64(time_raw);
    now      = g_get_real_time() / G_USEC_PER_SEC;

    if (then <= 0 || now < then || now - then > (gint64)ttl)
        return FALSE;

    {
        g_autofree gchar *url = g_key_file_get_string(kf, "resolve", "url", NULL);

        if (url == NULL || *url == '\0')
            return FALSE;

        source_clear(out);

        out->url      = g_steal_pointer(&url);
        out->headers  = g_key_file_get_string(kf, "resolve", "headers", NULL);
        out->title    = g_key_file_get_string(kf, "resolve", "title", NULL);
        out->artist   = g_key_file_get_string(kf, "resolve", "artist", NULL);
        out->ranged   = g_key_file_get_boolean(kf, "resolve", "ranged", NULL);
        out->resolved = TRUE;
        out->from_cache = TRUE;
    }

    if (out->headers != NULL && *out->headers == '\0')
        g_clear_pointer(&out->headers, g_free);
    if (out->title != NULL && *out->title == '\0')
        g_clear_pointer(&out->title, g_free);
    if (out->artist != NULL && *out->artist == '\0')
        g_clear_pointer(&out->artist, g_free);

    return TRUE;
}

void resolvecache_save(const gchar *uri, const MediaSource *src)
{
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autofree gchar *path = NULL;
    g_autofree gchar *data = NULL;
    gsize             len  = 0;

    if (uri == NULL || *uri == '\0' || src == NULL ||
        !src->resolved || src->url == NULL || *src->url == '\0' ||
        config_resolve_ttl() <= 0)
        return;

    path = resolve_path(uri);

    {
        g_autofree gchar *time_text = g_strdup_printf(
            "%" G_GINT64_FORMAT, g_get_real_time() / G_USEC_PER_SEC);

        g_key_file_set_string(kf, "resolve", "time", time_text);
    }

    g_key_file_set_string(kf, "resolve", "url", src->url);
    g_key_file_set_string(kf, "resolve", "headers",
                          src->headers != NULL ? src->headers : "");
    g_key_file_set_string(kf, "resolve", "title",
                          src->title != NULL ? src->title : "");
    g_key_file_set_string(kf, "resolve", "artist",
                          src->artist != NULL ? src->artist : "");
    g_key_file_set_boolean(kf, "resolve", "ranged", src->ranged);

    data = g_key_file_to_data(kf, &len, NULL);

    if (data != NULL)
        g_file_set_contents(path, data, (gssize)len, NULL);
}

void resolvecache_invalidate(const gchar *uri)
{
    g_autofree gchar *path = NULL;

    if (uri == NULL || *uri == '\0')
        return;

    path = resolve_path(uri);
    g_unlink(path);
}

void resolvecache_prune(void)
{
    g_autofree gchar *dir = follow_cache_dir();
    GDir             *d;
    gint              ttl = config_resolve_ttl();
    gint64            now = g_get_real_time() / G_USEC_PER_SEC;

    if (ttl <= 0)
        return;

    d = g_dir_open(dir, 0, NULL);
    if (d == NULL)
        return;

    for (const gchar *name = g_dir_read_name(d); name != NULL;
         name = g_dir_read_name(d)) {
        g_autoptr(GKeyFile) kf = g_key_file_new();
        g_autofree gchar *path = NULL;
        g_autofree gchar *time_raw = NULL;
        gint64            then;

        if (!g_str_has_prefix(name, "resolve-"))
            continue;

        path = g_build_filename(dir, name, NULL);
        if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
            continue;

        time_raw = g_key_file_get_string(kf, "resolve", "time", NULL);
        then     = parse_int64(time_raw);

        if (then <= 0 || now < then || now - then > (gint64)ttl)
            g_unlink(path);
    }

    g_dir_close(d);
}
