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

#ifndef FOLLOW_WEBDAVSETTINGS_H
#define FOLLOW_WEBDAVSETTINGS_H

#include <glib.h>

/* WebDAV 同步设置。密码不落磁盘，存放在系统密钥环。 */

typedef struct {
    gchar  *url;          /* 远端目录，如 https://dav.example.com/.../Follow */
    gchar  *username;
    gboolean auto_backup; /* 退出时自动备份 */
    gint64  last_backup;  /* Unix 秒，0 = 从未备份 */
} WebDavSettings;

void     webdav_settings_load(WebDavSettings *settings);
gboolean webdav_settings_save(const WebDavSettings *settings);
void     webdav_settings_clear(WebDavSettings *settings);
gchar   *webdav_settings_path(void);

gboolean webdav_settings_valid(const WebDavSettings *settings);

/* 密码读写：account 使用 url，service 固定。 */
gboolean webdav_settings_store_password(const WebDavSettings *settings,
                                        const gchar *password, gchar **error);
gchar   *webdav_settings_load_password(const WebDavSettings *settings,
                                       gchar **error);
gboolean webdav_settings_clear_password(const WebDavSettings *settings,
                                        gchar **error);

#endif /* FOLLOW_WEBDAVSETTINGS_H */
