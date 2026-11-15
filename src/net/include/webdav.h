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

#ifndef FOLLOW_WEBDAV_H
#define FOLLOW_WEBDAV_H

#include <glib.h>

/* 最小 WebDAV 客户端：目录创建、PUT、GET、连接测试。 */

typedef struct {
    gchar *base_url;
    gchar *auth;        /* "user:password"，可为 NULL（匿名） */
} WebDavClient;

void webdav_client_init(WebDavClient *client, const gchar *base_url,
                        const gchar *username, const gchar *password);
void webdav_client_clear(WebDavClient *client);

/* 创建远端目录；目录已存在也算成功。 */
gboolean webdav_test(WebDavClient *client, gchar **error);

gboolean webdav_put(WebDavClient *client, const gchar *name,
                    const void *data, gsize length, gchar **error);
gboolean webdav_get(WebDavClient *client, const gchar *name,
                    gchar **data, gsize *length, gboolean *missing,
                    gchar **error);

#endif /* FOLLOW_WEBDAV_H */
