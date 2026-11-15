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
#ifndef FOLLOW_UIDBG_H
#define FOLLOW_UIDBG_H

#include <gtk/gtk.h>

#include "theme.h"

/* 调试通道（MCP）：注入按键 / 点击并读取画面记录。 */

enum {
    UIDBG_LAYER_WALL = 0,
    UIDBG_LAYER_WALL_CHROME,
    UIDBG_LAYER_STAGE,
    UIDBG_LAYER_LYRIC,
    UIDBG_LAYER_PALETTE,
    UIDBG_LAYER_PERF,
    UIDBG_LAYER_MAX
};

gboolean uidbg_enabled(void);
void     uidbg_start(GtkWidget *window);
void     uidbg_stop(void);

void uidbg_begin_layer(int layer);

void uidbg_layer_clear(int layer);

void uidbg_record_text(double x, double y, double w, double h,
                       double px, Rgba color, const gchar *text);
void uidbg_record_rect(double x, double y, double w, double h,
                       Rgba color, const gchar *tag);

void uidbg_frame_begin(void);
void uidbg_frame_end(void);

void uidbg_suppress(gboolean on);

gboolean uidbg_hook_key(guint keyval, GdkModifierType state);
void     uidbg_hook_click(double x, double y, gboolean double_click);
void     uidbg_hook_scroll(double x, double y, double dx, double dy);
void     uidbg_hook_motion(double x, double y);
void     uidbg_hook_text(const gchar *text);

void     uidbg_hook_seek(double seconds);

#endif
