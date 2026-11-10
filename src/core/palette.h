/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_PALETTE_H
#define FOLLOW_PALETTE_H

#include <glib.h>

/* 从封面里取几个种子色，用来生成动态背景。
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

/* 两组种子色按 t 插值（换封面时让背景颜色平滑过渡，而不是硬切） */
Palette palette_mix(const Palette *a, const Palette *b, double t);

#endif /* FOLLOW_PALETTE_H */
