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

#ifndef FOLLOW_LYRICVIEW_H
#define FOLLOW_LYRICVIEW_H

#include "config.h"

/* 歌词（解析本身在 lyrics.c）：
 *   加载：ini 里的 lrc= → 本地 .lrc / netease:xxx / qq:xxx（在线源后台线程 + 缓存）
 *         都没写就自动找音频旁边的同名 .lrc
 *   显示：按播放位置（减去偏移）取当前行
 *   偏移：[ / ] 微调 250ms，\ 回到 ini 里的值
 */

void lyricview_load(const AudioTrack *track);    /* 换曲目 */
void lyricview_sync(double position_sec);        /* 定时器里调：更新当前行 */
void lyricview_shift_offset(int delta_ms);       /* [ / ] */
void lyricview_reset_offset(void);               /* \ */
void lyricview_clear(void);                      /* 退出时 */

#endif /* FOLLOW_LYRICVIEW_H */
