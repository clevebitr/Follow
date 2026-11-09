#ifndef FOLLOW_TEXTURE_H
#define FOLLOW_TEXTURE_H

#include <cairo.h>
#include <glib.h>

/* 只有两件事：
 *   1. 非预乘 RGBA(8bit) → cairo 的预乘 ARGB32 surface
 *   2. 顺便把长边限制在 max_dim 以内（大封面先降采样一次，
 *      否则每帧把 3000px 的原图缩到 300px 会让 cairo 慢上百倍）
 *
 * 注意：不再用 gdk_texture_download 取像素 —— 它返回的通道序是 BGRA，
 * 而纹理的声明格式可能是 R8G8B8，按 RGBA 解会把红蓝搞反。解码统一走 gdk-pixbuf。
 */

cairo_surface_t *rgba_to_cairo_surface(const guchar *rgba, int width, int height);
cairo_surface_t *rgba_to_cairo_surface_fit(const guchar *rgba, int width, int height,
                                           int max_dim);

/* ARGB32 surface 的盒式模糊（可分离，passes 次 ≈ 高斯）。
 * 背景先缩到 ~160px 再模糊，最后放大，比全尺寸卷积快几个数量级。 */
void surface_box_blur(cairo_surface_t *surface, int radius, int passes);

#endif /* FOLLOW_TEXTURE_H */
