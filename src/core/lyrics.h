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

#endif /* FOLLOW_LYRICS_H */
