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

#ifndef FOLLOW_LYRICS_H
#define FOLLOW_LYRICS_H

#include <glib.h>

/* LRC 解析：多时间戳、offset、逐字时间轴；非 UTF-8 文本按 GB18030 转换。 */

typedef struct {
    gint64 time_ms;
    gchar *text;

    /* char_ms[i] = 第 i 个字的绝对时间（毫秒）；NULL = 整行高亮。 */
    gint64 *char_ms;
    gsize   n_chars;
} LyricLine;

typedef struct {
    LyricLine *lines;
    gsize      n_lines;
    gchar     *title;
    gchar     *artist;
} Lyrics;

/* 读取 .lrc；失败或无时间戳时返回 NULL。 */
Lyrics *lyrics_load_file(const char *path);

Lyrics *lyrics_load_text(const gchar *content);

void lyrics_free(Lyrics *lyrics);

/* 返回指定时刻的歌词行。 */
const gchar *lyrics_line_at(const Lyrics *lyrics, double seconds);

const gchar *lyrics_text_at(const Lyrics *lyrics, gssize index);

gssize lyrics_index_at(const Lyrics *lyrics, double seconds);

/* 焦点行：跳过只有时间戳的空行，返回最近一行有字的内容。 */
gssize lyrics_focus_at(const Lyrics *lyrics, double seconds);

gssize lyrics_next_index(const Lyrics *lyrics, gssize index);
gssize lyrics_prev_index(const Lyrics *lyrics, gssize index);

const gint64 *lyrics_char_times(const Lyrics *lyrics, gssize index, gsize *n_chars);

const gchar *lyrics_line_next(const Lyrics *lyrics, double seconds);

#endif
