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

#ifndef FOLLOW_PLATFORM_H
#define FOLLOW_PLATFORM_H

#include <cairo.h>
#include <glib.h>

/* 曲目来源平台：决定徽标图标、颜色与名称。 */

typedef enum {
    PLATFORM_UNKNOWN = 0,
    PLATFORM_BILIBILI,
    PLATFORM_APPLE_MUSIC,
    PLATFORM_YOUTUBE,
    PLATFORM_SPOTIFY,
    PLATFORM_SOUNDCLOUD,
    PLATFORM_NETEASE,
    PLATFORM_BANDCAMP
} Platform;

Platform    platform_from_url(const char *url);
Platform    platform_from_name(const char *name);

const char *platform_name(Platform p);
void        platform_brand_color(Platform p, double *r, double *g, double *b);

void        platform_draw_icon(cairo_t *cr, Platform p, double x, double y, double size);

/* 绘制官方字标；无字标时返回 FALSE。 */
gboolean    platform_wordmark_size(Platform p, double max_h, double max_w,
                                   double *out_w, double *out_h);
void        platform_draw_wordmark(cairo_t *cr, Platform p, double x, double y,
                                   double w, double h);

#endif
