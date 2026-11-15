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

#ifndef FOLLOW_METADB_H
#define FOLLOW_METADB_H

#include <glib.h>

#include "metadata.h"

/* music 模块内部：元数据 SQLite 持久化，仅 UI 线程调用。 */

gboolean metadb_open(void);
void     metadb_close(void);

gboolean metadb_get(const gchar *key, TrackMeta **out);
gboolean metadb_put(const TrackMeta *meta);
void     metadb_remove(const gchar *key);

/* 候选缓存；离线时仍可用于指认。 */
GPtrArray *metadb_get_candidates(const gchar *key);
gboolean   metadb_put_candidates(const gchar *key, const GPtrArray *candidates);
void       metadb_clear_candidates(const gchar *key);

#endif
