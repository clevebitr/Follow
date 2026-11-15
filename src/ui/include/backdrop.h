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

#ifndef FOLLOW_BACKDROP_H
#define FOLLOW_BACKDROP_H

#include <cairo.h>
#include <glib.h>

#include "palette.h"

/* 动态背景：模糊封面、种子色漂移光斑与压暗。CPU 与 GPU 路径共用 BackdropFrame。 */

typedef struct {
    float cx, cy, r, alpha;
    float cr, cg, cb;
} BackdropBlob;

typedef struct {
    cairo_surface_t *base;
    cairo_surface_t *prev;
    int              base_w, base_h;
    gint64           base_gen;
    gint64           prev_gen;
    gboolean         has_prev;
    double           tide;
    double           phase;
    gboolean         has_palette;
    Palette          palette;
    BackdropBlob     blob[3];
} BackdropFrame;

/* 换封面：保留旧底图用于退潮过渡。 */
void backdrop_swap(void);

void backdrop_invalidate(void);

void backdrop_drop(void);
void backdrop_push(int width, int height);
gboolean backdrop_tick(gpointer data);

void backdrop_step(double dt);

const BackdropFrame *backdrop_prepare(int width, int height);

#endif
