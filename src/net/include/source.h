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

#ifndef FOLLOW_SOURCE_H
#define FOLLOW_SOURCE_H

#include <glib.h>

/* 将 ini link 解析为可播放直链，并补充标题、请求头等元数据。 */
typedef struct {
    gchar   *url;
    gchar   *headers;
    gchar   *title;
    gchar   *artist;
    gboolean resolved;
    gboolean from_cache;
    gboolean ranged;    /* TRUE = 使用定长 Range 分块读。 */
} MediaSource;

int  source_resolve(const char *uri, MediaSource *out, gchar **error);
void source_clear(MediaSource *s);

void source_cache_invalidate(const char *uri);

#endif
