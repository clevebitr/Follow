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

#ifndef FOLLOW_LYRICSRC_H
#define FOLLOW_LYRICSRC_H

#include <glib.h>

/* 在线歌词源：按用户给的歌名/歌手或歌曲 id 去取带时间轴的 LRC。
 *
 * 配置写法（[audioN] 的 lrc=）：
 *   lrc=netease                 → 用 title + artist 自动搜索
 *   lrc=netease:耳朵 李荣浩      → 用这个关键词搜索
 *   lrc=netease:186016          → 直接指定歌曲 id
 *   lrc=qq / qq:<关键词> / qq:<songmid>
 *   lrc=X:/x.lrc                → 本地文件（不经过这里）
 */

typedef struct {
    const gchar *provider;   /* "netease" / "qq" */
    const gchar *spec;       /* provider 后面那段；可为 NULL/空 */
    const gchar *artist;     /* 自动搜索兜底用 */
    const gchar *title;
} LyricsQuery;

/* 成功返回 LRC 文本（新分配的 UTF-8），失败返回 NULL 并给 *error。
 * 返回的文本可能带 [ti:]/[ar:] 等元数据。 */
gchar *lyricsrc_fetch(const LyricsQuery *query, gchar **error);

/* "netease:xxx" → provider/spec；不是在线源返回 FALSE */
gboolean lyricsrc_parse(const gchar *spec, gchar **provider, gchar **rest);

#endif /* FOLLOW_LYRICSRC_H */
