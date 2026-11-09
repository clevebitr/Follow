#ifndef FOLLOW_LYRICS_H
#define FOLLOW_LYRICS_H

#include <glib.h>

/* LRC 支持两件事：
 *   1. 播放时按时间取当前行（lyrics_load_file → lyrics_line_at）
 *   2. 打轴模式：按文件顺序读出每行文本和它已有的时间戳（-1 = 还没打轴），
 *      打完再写回 .lrc（lyrics_read_raw / lyrics_save_timed）
 *
 * 这样"完全没有时间轴的纯文本歌词"也能用：先 load 不出来字幕，
 * 但可以进打轴模式，边听边盖章，写完就有时间轴了。
 */

typedef struct {
    gint64 time_ms;
    gchar *text;
} LyricLine;

typedef struct {
    LyricLine *lines;
    gsize      n_lines;
    gchar     *title;
    gchar     *artist;
} Lyrics;

/* 原始内容：按文件顺序，times[i] = -1 表示这行还没有时间戳 */
typedef struct {
    GPtrArray *texts;      /* gchar* */
    GArray    *times;      /* gint64  */
    gchar     *title;
    gchar     *artist;
} RawLyrics;

/* 读本地 .lrc（UTF-8；不是 UTF-8 就按 GB18030 转）。失败返回 NULL。 */
Lyrics *lyrics_load_file(const char *path);

/* 直接从一段 LRC 文本构建（在线歌词源拉回来的就是这种） */
Lyrics *lyrics_load_text(const gchar *content);
void    lyrics_free(Lyrics *lyrics);

/* 返回 seconds 时刻应该显示的那一行；没有歌词时返回 NULL。 */
const gchar *lyrics_line_at(const Lyrics *lyrics, double seconds);

/* 按文件顺序读原始内容（容忍没有时间戳的纯文本行） */
RawLyrics *lyrics_read_raw(const char *path);
void       lyrics_raw_free(RawLyrics *raw);

#endif /* FOLLOW_LYRICS_H */
