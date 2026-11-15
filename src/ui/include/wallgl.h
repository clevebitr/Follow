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

#ifndef FOLLOW_WALLGL_H
#define FOLLOW_WALLGL_H

#include <gtk/gtk.h>

/* 歌曲墙 GPU 层：纹理磁贴、相机平移、选中与淡入。 */

typedef struct {
    gsize    track;
    double   x, y, w, h;
    gboolean selected;
    gboolean hovered;
    gboolean current;
    float    reveal;
} WallGlTile;

typedef struct {
    WallGlTile *tiles;
    int         n_tiles;
    int         selected;
    double      view_w, view_h;

    double      dim_top, dim_bottom;
    double      bg_r, bg_g, bg_b;

    gboolean    tint;
    double      tint_seed_r, tint_seed_g, tint_seed_b;
    double      tint_alt_r, tint_alt_g, tint_alt_b;

    double      selected_pop;
    double      glow_r, glow_g, glow_b;
    gboolean    glow;

    double      vis;
    double      offset_y;
} WallGlFrame;

GtkWidget *wallgl_new(void);
gboolean   wallgl_ready(void);
int        wallgl_tile_scale(void);
void       wallgl_set_frame(const WallGlFrame *frame);
void       wallgl_set_active(gboolean active);
void       wallgl_clear(void);

#endif
