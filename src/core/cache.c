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
