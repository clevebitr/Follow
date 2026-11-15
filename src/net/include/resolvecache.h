/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_RESOLVECACHE_H
#define FOLLOW_RESOLVECACHE_H

#include <glib.h>

#include "source.h"

/* 页面链接到解析结果的磁盘缓存；TTL 由 [cache] resolve_ttl 控制。 */

gboolean resolvecache_load(const gchar *uri, MediaSource *out);
void     resolvecache_save(const gchar *uri, const MediaSource *src);
void     resolvecache_invalidate(const gchar *uri);
void     resolvecache_prune(void);

#endif
