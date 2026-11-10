/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#ifndef FOLLOW_TEXTURE_H
#define FOLLOW_TEXTURE_H

#include <cairo.h>
#include <glib.h>

/* 只有两件事：
 *   1. 非预乘 RGBA(8bit) → cairo 的预乘 ARGB32 surface
 *   2. 把长边限制在 max_dim 以内
 */

cairo_surface_t *rgba_to_cairo_surface(const guchar *rgba, int width, int height);
cairo_surface_t *rgba_to_cairo_surface_fit(const guchar *rgba, int width, int height,
                                           int max_dim);

/* ARGB32 surface 的盒式模糊 */
void surface_box_blur(cairo_surface_t *surface, int radius, int passes);

#endif /* FOLLOW_TEXTURE_H */
