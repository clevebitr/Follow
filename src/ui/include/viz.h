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

#ifndef FOLLOW_VIZ_H
#define FOLLOW_VIZ_H

#include <cairo.h>
#include <glib.h>

#include "palette.h"

/* 音频粒子背景：CPU cairo 与 GPU sprite 两条路径。 */

typedef struct {
    float x, y;
    float u, v;
    float r, g, b;
    float a;
} VizGlVertex;

void        viz_configure(const gchar *effect, int particles, double gain);
void        viz_reset(void);
gboolean    viz_active(void);
const gchar *viz_effect_name(void);

void        viz_tick(void);

void        viz_draw(cairo_t *cr, double w, double h, const Palette *pal);

/* 展开为加色混合的 sprite 四边形。 */
int         viz_gl_vertices(VizGlVertex *out, int max,
                            double w, double h, const Palette *pal);

void        viz_burst(void);

#endif
