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

#ifndef FOLLOW_PLAYER_H
#define FOLLOW_PLAYER_H

#include <glib.h>

gboolean player_play_index(gint index);

gboolean player_play_first(void);

void player_play_neighbour(gint dir);

gint player_queue_preview(void);

void player_queue_step(gint dir);

void player_toggle(void);
void player_restart(void);
void player_stop(void);
void player_seek_by(double delta);

void player_seek_to(double seconds);

void player_cycle_repeat(void);
void player_toggle_shuffle(void);

void player_check_finished(void);

void player_check_load_failure(void);

gchar *player_label(void);

void player_update_window_title(void);
void player_clear(void);

#endif
