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

#ifndef FOLLOW_PALETTE_H
#define FOLLOW_PALETTE_H

#include <glib.h>

/* 从封面提取背景种子色；灰度封面 has = FALSE。 */

typedef struct {
    double   seed_r, seed_g, seed_b;
    double   alt_r,  alt_g,  alt_b;
    double   deep_r, deep_g, deep_b;
    gboolean has;
} Palette;

void palette_from_rgba(const guchar *rgba, int width, int height, Palette *out);
void palette_reset(Palette *p);

Palette palette_mix(const Palette *a, const Palette *b, double t);

#endif
