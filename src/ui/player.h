#ifndef FOLLOW_PLAYER_H
#define FOLLOW_PLAYER_H

#include <glib.h>

/* 播放控制：播放列表顺序 / 随机 / 循环、网络地址解析、曲目显示名。
 *
 * 所有"切歌"都必须走 player_play_index()，它会一次性把封面、歌词、二维码、
 * 窗口标题全部换掉 —— 这是唯一的状态入口。
 */

/* 播放第 index 首；不可播放时返回 FALSE（只记日志，不动当前状态） */
gboolean player_play_index(gint index);

/* 从第一首开始找一首能放的 */
gboolean player_play_first(void);

/* dir = +1 下一首 / -1 上一首（随机模式下走历史） */
void player_play_neighbour(gint dir);

void player_toggle(void);        /* Space：播放 / 暂停 / 继续 / 从头开始 */
void player_restart(void);       /* R */
void player_stop(void);          /* S */
void player_seek_by(double delta);

void player_cycle_repeat(void);  /* L：off → all → one */
void player_toggle_shuffle(void);/* H */

/* 定时器里调：一首放完就按当前模式续播 */
void player_check_finished(void);

/* 显示用的「艺人 - 曲名」（ini > 网络解析 > 文件名），新分配的字符串 */
gchar *player_label(void);

void player_update_window_title(void);
void player_clear(void);

#endif /* FOLLOW_PLAYER_H */
