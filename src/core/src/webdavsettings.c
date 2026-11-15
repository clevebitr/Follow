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

#include "webdavsettings.h"

#include <string.h>

#include <glib/gstdio.h>

#include "cache.h"
#include "keyring.h"

#define WEBDAV_KEYRING_SERVICE "org.github.clevebitr.follow.webdav"

gchar *webdav_settings_path(void)
{
    g_autofree gchar *dir = follow_cache_dir();

    return g_build_filename(dir, "webdav.ini", NULL);
}

void webdav_settings_clear(WebDavSettings *settings)
{
    if (settings == NULL)
        return;

    g_free(settings->url);
    g_free(settings->username);
    *settings = (WebDavSettings){ 0 };
}

void webdav_settings_load(WebDavSettings *settings)
{
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autofree gchar   *path = webdav_settings_path();

    if (settings == NULL)
        return;

    *settings = (WebDavSettings){ 0 };

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
        return;

    settings->url = g_key_file_get_string(kf, "webdav", "url", NULL);
    settings->username = g_key_file_get_string(kf, "webdav", "username", NULL);
    settings->auto_backup = g_key_file_get_boolean(kf, "webdav",
                                                   "auto_backup", NULL);
    settings->last_backup = g_key_file_get_int64(kf, "webdav",
                                                 "last_backup", NULL);

    if (settings->url != NULL && *settings->url == '\0')
        g_clear_pointer(&settings->url, g_free);

    if (settings->username != NULL && *settings->username == '\0')
        g_clear_pointer(&settings->username, g_free);
}

gboolean webdav_settings_save(const WebDavSettings *settings)
{
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autofree gchar   *path = webdav_settings_path();
    g_autofree gchar   *data = NULL;
    gsize               len = 0;

    if (settings == NULL)
        return FALSE;

    g_key_file_set_string(kf, "webdav", "url",
                          settings->url != NULL ? settings->url : "");
    g_key_file_set_string(kf, "webdav", "username",
                          settings->username != NULL ? settings->username : "");
    g_key_file_set_boolean(kf, "webdav", "auto_backup", settings->auto_backup);
    g_key_file_set_int64(kf, "webdav", "last_backup", settings->last_backup);

    data = g_key_file_to_data(kf, &len, NULL);
    if (data == NULL || len == 0)
        return FALSE;

    if (!g_file_set_contents(path, data, (gssize)len, NULL))
        return FALSE;

    g_chmod(path, 0600);
    return TRUE;
}

gboolean webdav_settings_valid(const WebDavSettings *settings)
{
    if (settings == NULL || settings->url == NULL || *settings->url == '\0')
        return FALSE;

    return g_str_has_prefix(settings->url, "https://") ||
           g_str_has_prefix(settings->url, "http://");
}

static const gchar *password_account(const WebDavSettings *settings)
{
    if (settings == NULL)
        return "";

    if (settings->url != NULL && *settings->url != '\0')
        return settings->url;

    return settings->username != NULL ? settings->username : "";
}

gboolean webdav_settings_store_password(const WebDavSettings *settings,
                                        const gchar *password, gchar **error)
{
    return keyring_store(WEBDAV_KEYRING_SERVICE, password_account(settings),
                         password, error);
}

gchar *webdav_settings_load_password(const WebDavSettings *settings,
                                     gchar **error)
{
    return keyring_lookup(WEBDAV_KEYRING_SERVICE, password_account(settings),
                          error);
}

gboolean webdav_settings_clear_password(const WebDavSettings *settings,
                                        gchar **error)
{
    return keyring_delete(WEBDAV_KEYRING_SERVICE, password_account(settings),
                          error);
}
