/**
 * created: 2026/11/11
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#include "settings.h"

#include <glib/gstdio.h>

#include "cache.h"

static struct {
    double   volume;
    gboolean muted;
    gchar   *output;
    gboolean loaded;
} S = { 1.0, FALSE, NULL, FALSE };

static gchar *settings_path(void)
{
    gchar *dir  = follow_cache_dir();
    gchar *path = g_build_filename(dir, "settings.ini", NULL);

    g_free(dir);
    return path;
}

void settings_load(void)
{
    g_autoptr(GKeyFile) kf   = g_key_file_new();
    g_autofree gchar   *path = settings_path();
    GError             *error = NULL;
    gdouble             volume;

    S.volume = 1.0;
    S.muted  = FALSE;
    g_clear_pointer(&S.output, g_free);

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &error)) {
        g_clear_error(&error);
        S.loaded = TRUE;
        return;
    }

    volume = g_key_file_get_double(kf, "audio", "volume", &error);

    if (error == NULL)
        S.volume = CLAMP(volume, 0.0, 1.0);
    else
        g_clear_error(&error);

    S.muted = g_key_file_get_boolean(kf, "audio", "muted", NULL);

    {
        gchar *output = g_key_file_get_string(kf, "audio", "output", NULL);

        if (output != NULL && *output != '\0' &&
            g_ascii_strcasecmp(output, "default") != 0)
            S.output = output;
        else
            g_free(output);
    }

    S.loaded = TRUE;
}

void settings_save(void)
{
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autofree gchar   *path = settings_path();
    g_autofree gchar   *data = NULL;
    gsize               len  = 0;

    if (!S.loaded)
        return;

    g_key_file_set_double(kf, "audio", "volume", S.volume);
    g_key_file_set_boolean(kf, "audio", "muted", S.muted);
    g_key_file_set_string(kf, "audio", "output",
                          S.output != NULL ? S.output : "default");

    data = g_key_file_to_data(kf, &len, NULL);

    if (data != NULL && len > 0)
        g_file_set_contents(path, data, (gssize)len, NULL);
}

void settings_clear(void)
{
    g_clear_pointer(&S.output, g_free);
    S.volume = 1.0;
    S.muted  = FALSE;
    S.loaded = FALSE;
}

double settings_volume(void)
{
    return S.volume;
}

void settings_set_volume(double volume)
{
    S.volume = CLAMP(volume, 0.0, 1.0);
    settings_save();
}

gboolean settings_muted(void)
{
    return S.muted;
}

void settings_set_muted(gboolean muted)
{
    S.muted = muted ? TRUE : FALSE;
    settings_save();
}

const gchar *settings_output_device(void)
{
    return S.output;
}

void settings_set_output_device(const gchar *name)
{
    g_free(S.output);
    S.output = (name != NULL && *name != '\0' &&
                g_ascii_strcasecmp(name, "default") != 0)
        ? g_strdup(name) : NULL;

    settings_save();
}
