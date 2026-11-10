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

gchar *follow_cache_dir(void)
{
    gchar *dir = g_build_filename(g_get_user_cache_dir(), "follow", NULL);

    g_mkdir_with_parents(dir, 0700);

    return dir;
}

gchar *follow_cache_file(const gchar *prefix, const gchar *key, const gchar *ext)
{
    g_autofree gchar *dir = follow_cache_dir();
    g_autofree gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_MD5,
                                                             key != NULL ? key : "", -1);

    return g_strdup_printf("%s%c%s-%s.%s", dir, G_DIR_SEPARATOR, prefix, digest,
                           ext != NULL ? ext : "bin");
}
