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

#ifndef FOLLOW_KEYRING_H
#define FOLLOW_KEYRING_H

#include <glib.h>

/* 系统密钥环：Windows Credential Manager / Linux Secret Service。
 * service / account 是逻辑键；secret 为 UTF-8 字符串。 */
gboolean keyring_store(const gchar *service, const gchar *account,
                       const gchar *secret, gchar **error);
gchar   *keyring_lookup(const gchar *service, const gchar *account,
                        gchar **error);
gboolean keyring_delete(const gchar *service, const gchar *account,
                        gchar **error);

#endif /* FOLLOW_KEYRING_H */
