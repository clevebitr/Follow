/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_LYRICS_H
#define FOLLOW_LYRICS_H

#include <glib.h>

/* LRC 解析：
 *
 * 支持：`[mm:ss.xx]` 时间戳（一行多个）、`[offset:]`、`[ti:]/[ar:]`、
 * 行内 `<mm:ss.xx>`（直接剥掉）；不是 UTF-8 的按 GB18030 转换（中文 .lrc 大多是 GBK）。 */

typedef struct {
    gint64 time_ms;
    gchar *text;

    /* 逐字时间轴（增强 LRC 的 <mm:ss.xx> / 网易云 yrc 的 (start,dur,0)）：
     * char_ms[i] = 第 i 个字的绝对时间（毫秒），n_chars = 这一行几个字。
     * NULL = 这句没有逐字时间，渲染时整行一起高亮。 */
    gint64 *char_ms;
    gsize   n_chars;
} LyricLine;

typedef struct {
    LyricLine *lines;      /* 按时间升序 */
    gsize      n_lines;
    gchar     *title;
    gchar     *artist;
} Lyrics;

/* 读本地 .lrc；读不到；没有一行带时间戳就返回 NULL */
Lyrics *lyrics_load_file(const char *path);

/* 直接从一段 LRC 文本构建 */
Lyrics *lyrics_load_text(const gchar *content);

void lyrics_free(Lyrics *lyrics);

/* 返回 seconds 时刻应该显示的那一行；没有歌词 / 还没到第一行时返回 NULL */
const gchar *lyrics_line_at(const Lyrics *lyrics, double seconds);

/* 第 index 行的文字（越界 / 空行返回 NULL） */
const gchar *lyrics_text_at(const Lyrics *lyrics, gssize index);

/* 同一时刻的行号：-1 = 还没到第一行（显示时当"前奏"） */
gssize lyrics_index_at(const Lyrics *lyrics, double seconds);

/* 焦点行：位置所在的那一行，但遇到只有时间戳的空行（间奏 / 占位）就往回退到
 * 最近一行有字的行。歌里句间的空隙、长间奏都不会把画面清空。
 * 返回 -1 表示还一行都没唱到。 */
gssize lyrics_focus_at(const Lyrics *lyrics, double seconds);

/* 从 index 往后 / 往前找第一行有字的行；没有返回 -1 */
gssize lyrics_next_index(const Lyrics *lyrics, gssize index);
gssize lyrics_prev_index(const Lyrics *lyrics, gssize index);

/* 第 index 行的逐字时间轴（NULL = 整行高亮）；*n_chars 回传这一行的字数 */
const gint64 *lyrics_char_times(const Lyrics *lyrics, gssize index, gsize *n_chars);

/* 预显示用的下一行：从当前行往后找第一行有字的（LRC 里常用空行占位）；
 * 没有下一行时返回 NULL */
const gchar *lyrics_line_next(const Lyrics *lyrics, double seconds);

#endif /* FOLLOW_LYRICS_H */
