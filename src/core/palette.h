#ifndef FOLLOW_PALETTE_H
#define FOLLOW_PALETTE_H

#include <glib.h>

/* 从封面里取几个"种子色"，用来生成动态背景。
 * 思路和 Android Palette / color-thief / Apple Music 那套一样：
 * 按 饱和度×明度 加权统计色相直方图，取最重的一簇当主色，再取一个拉开色相的第二色。
 * 灰度封面取不到色时 has = FALSE，背景就只用模糊封面 + 压暗。 */

typedef struct {
    double   seed_r, seed_g, seed_b;   /* 主色（较饱和） */
    double   alt_r,  alt_g,  alt_b;    /* 第二主色（色相和主色拉开） */
    double   deep_r, deep_g, deep_b;   /* 主色压暗，用来铺底部 */
    gboolean has;
} Palette;

void palette_from_rgba(const guchar *rgba, int width, int height, Palette *out);
void palette_reset(Palette *p);

#endif /* FOLLOW_PALETTE_H */
