/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_LYRICGL_H
#define FOLLOW_LYRICGL_H

#include <gtk/gtk.h>

#include "lyrics.h"
#include "theme.h"

/* 歌词 GPU 层：CPU 排版为 A8 遮罩，扫描、模糊与渐隐由 GPU 完成。 */

typedef struct {
    double x, w;
    double top;
    double height;
    double px;
    double gap;
    double position_ms;
    gboolean playing;
    Rgba   accent;      /* 当前句高亮色；a <= 0 时使用白色。 */
} LyricViewGeom;

GtkWidget *lyricgl_new(void);

gboolean   lyricgl_ready(void);

/* 更新视图并重新排版。 */
void       lyricgl_set_view(const Lyrics *lyrics, gssize index,
                            const LyricViewGeom *geom);

void       lyricgl_clear(void);

/* 拖动 / 缩放期间暂停重绘。 */
void       lyricgl_drag_state(gboolean dragging);

#endif
