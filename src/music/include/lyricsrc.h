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

#ifndef FOLLOW_LYRICSRC_H
#define FOLLOW_LYRICSRC_H

#include <glib.h>

/* 在线歌词源：按关键词或歌曲 id 获取带时间轴的 LRC。 */

typedef struct {
    const gchar *provider;
    const gchar *spec;
    const gchar *artist;
    const gchar *title;
} LyricsQuery;

/* 成功返回 UTF-8 LRC 文本；失败返回 NULL。 */
gchar *lyricsrc_fetch(const LyricsQuery *query, gchar **error);

gboolean lyricsrc_parse(const gchar *spec, gchar **provider, gchar **rest);

#endif
