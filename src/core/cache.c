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

#include "cache.h"

#include <errno.h>

#include "config.h"

/* 解析 [cache] dir：
 *   NULL / ""    → <用户缓存目录>/follow（默认）
 *   "~/xxx"      → <家目录>/xxx
 *   "D:/xxx"     → 原样（规范化）
 *   "cache/xxx"  → 相对当前工作目录 */
static gchar *cache_dir_resolve(const gchar *raw)
{
    if (raw == NULL || *raw == '\0')
        return g_build_filename(g_get_user_cache_dir(), "follow", NULL);

    if (g_str_has_prefix(raw, "~/") || g_str_has_prefix(raw, "~\\"))
        return g_build_filename(g_get_home_dir(), raw + 2, NULL);

    if (g_strcmp0(raw, "~") == 0)
        return g_strdup(g_get_home_dir());

    return g_canonicalize_filename(raw, NULL);
}

gchar *follow_cache_dir(void)
{
    static gchar *resolved = NULL;      /* 配置只会 load 一次，结果算一遍就够 */

    if (resolved == NULL) {
        resolved = cache_dir_resolve(config_get()->cache_dir);

        if (g_mkdir_with_parents(resolved, 0700) != 0)
            g_warning("cache: cannot create %s (%s)", resolved, g_strerror(errno));
        else
            g_debug("cache: dir = %s", resolved);
    }

    return g_strdup(resolved);
}

gchar *follow_cache_file(const gchar *prefix, const gchar *key, const gchar *ext)
{
    g_autofree gchar *dir = follow_cache_dir();
    g_autofree gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_MD5,
                                                             key != NULL ? key : "", -1);

    return g_strdup_printf("%s%c%s-%s.%s", dir, G_DIR_SEPARATOR, prefix, digest,
                           ext != NULL ? ext : "bin");
}
