/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_DETAILS_H
#define FOLLOW_DETAILS_H

#include <cairo.h>
#include <gdk/gdk.h>
#include <glib.h>

/* 歌曲详情页（默认快捷键 i），整屏 cairo 绘制。数据来自 metadata 状态机。 */

void     details_open(void);
void     details_close(void);
void     details_toggle(void);
gboolean details_is_open(void);

/* 详情页按键分发；返回 TRUE 表示事件已消费。 */
gboolean details_key(guint keyval, GdkModifierType state);

void details_draw(cairo_t *cr, double width, double height);

#endif
