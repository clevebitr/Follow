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

#include "bilibili.h"
#include "config.h"
#include "platform.h"
#include "resolvecache.h"
#include "source.h"
#include "youtube.h"

int source_resolve(const char *uri, MediaSource *out, gchar **error)
{
    if (out == NULL)
        return -1;

    out->url        = NULL;
    out->headers    = NULL;
    out->title      = NULL;
    out->artist     = NULL;
    out->resolved   = FALSE;
    out->from_cache = FALSE;
    out->ranged     = FALSE;

    if (error != NULL)
        *error = NULL;

    if (uri == NULL || *uri == '\0') {
        if (error != NULL)
            *error = g_strdup("空的 link");
        return -1;
    }

    if (!config_is_remote(uri)) {
        out->url = g_strdup(uri);
        return 0;
    }

    /* 页面链接：先看解析缓存，命中就不打 API（直链过期由 player 负责失效重试） */
    if (resolvecache_load(uri, out))
        return 0;

    if (platform_from_url(uri) == PLATFORM_YOUTUBE) {
        gchar *audio_url = NULL;
        gchar *title = NULL;
        gchar *artist = NULL;
        gchar *why = NULL;

        if (youtube_resolve(uri, &audio_url, &title, &artist, &why) != 0) {
            if (error != NULL)
                *error = why != NULL ? why : g_strdup("YouTube 解析失败");
            else
                g_free(why);

            g_free(audio_url);
            g_free(title);
            g_free(artist);
            return -1;
        }

        g_free(why);

        out->url      = audio_url;
        out->headers  = youtube_request_headers();
        out->title    = title;
        out->artist   = artist;
        out->resolved = TRUE;
        out->ranged   = TRUE;   /* googlevideo 只认定长 Range 请求 */

        resolvecache_save(uri, out);
        g_message("source: youtube -> %s (%s)", title != NULL ? title : "?", audio_url);
        return 0;
    }

    if (platform_from_url(uri) == PLATFORM_BILIBILI) {
        gchar *audio_url = NULL;
        gchar *title = NULL;
        gchar *artist = NULL;
        gchar *why = NULL;

        if (bilibili_resolve(uri, &audio_url, &title, &artist, &why) != 0) {
            if (error != NULL)
                *error = why != NULL ? why : g_strdup("B 站解析失败");
            else
                g_free(why);

            g_free(audio_url);
            g_free(title);
            g_free(artist);
            return -1;
        }

        g_free(why);

        out->url      = audio_url;
        out->headers  = bilibili_request_headers();
        out->title    = title;
        out->artist   = artist;
        out->resolved = TRUE;

        resolvecache_save(uri, out);
        g_message("source: bilibili -> %s (%s)", title != NULL ? title : "?", audio_url);
        return 0;
    }

    /* 其它 http(s) 直链：原样交给解码器 */
    out->url = g_strdup(uri);
    return 0;
}

void source_cache_invalidate(const char *uri)
{
    resolvecache_invalidate(uri);
}

void source_clear(MediaSource *s)
{
    if (s == NULL)
        return;

    g_free(s->url);
    g_free(s->headers);
    g_free(s->title);
    g_free(s->artist);
    memset(s, 0, sizeof(*s));
}
