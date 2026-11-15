/**
 * created: 2026/11/11
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_CMDBOX_H
#define FOLLOW_CMDBOX_H

#include <gtk/gtk.h>

/* 命令面板：Ctrl+Shift+P 开关，↑↓ 选择，Enter 执行，Esc 关闭。 */

GtkWidget *palette_new(void);
void       palette_open(void);
void       palette_open_query(const gchar *query);
void       palette_close(void);
void       palette_toggle(void);
gboolean   palette_is_open(void);
gboolean   palette_contains(double x, double y);

void       palette_debug_steps(const gchar *steps);

#endif
