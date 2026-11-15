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

#ifndef FOLLOW_TEXTURE_H
#define FOLLOW_TEXTURE_H

#include <cairo.h>
#include <glib.h>

/* RGBA → 预乘 ARGB32 surface，并将长边限制在 max_dim 内。 */

cairo_surface_t *rgba_to_cairo_surface(const guchar *rgba, int width, int height);
cairo_surface_t *rgba_to_cairo_surface_fit(const guchar *rgba, int width, int height,
                                           int max_dim);

void surface_box_blur(cairo_surface_t *surface, int radius, int passes);

#endif
