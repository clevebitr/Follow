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

#include <string.h>

#include "texture.h"

static const cairo_user_data_key_t g_surface_pixels_key;

/* 直通 RGBA（非预乘）→ cairo 的 ARGB32（预乘；小端下内存序是 B,G,R,A） */
cairo_surface_t *rgba_to_cairo_surface(const guchar *rgba, int width, int height)
{
    guchar          *data;
    int              stride;
    cairo_surface_t *surface;

    if (rgba == NULL || width <= 0 || height <= 0)
        return NULL;

    stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    data   = g_malloc0_n((gsize)stride, (gsize)height);

    for (int y = 0; y < height; y++) {
        const guchar *s = rgba + (gsize)y * (gsize)width * 4;
        guchar       *d = data + (gsize)y * (gsize)stride;

        for (int x = 0; x < width; x++) {
            unsigned int a = s[x * 4 + 3];

            d[x * 4 + 0] = (guchar)((unsigned int)s[x * 4 + 2] * a / 255u);
            d[x * 4 + 1] = (guchar)((unsigned int)s[x * 4 + 1] * a / 255u);
            d[x * 4 + 2] = (guchar)((unsigned int)s[x * 4 + 0] * a / 255u);
            d[x * 4 + 3] = (guchar)a;
        }
    }

    surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
                                                  width, height, stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        g_free(data);
        return NULL;
    }

    /* 让 surface 负责释放这块内存 */
    cairo_surface_set_user_data(surface, &g_surface_pixels_key, data, g_free);

    return surface;
}

cairo_surface_t *rgba_to_cairo_surface_fit(const guchar *rgba, int width, int height,
                                           int max_dim)
{
    cairo_surface_t *src;
    cairo_surface_t *dst;
    cairo_t         *cr;
    int              dw, dh;
    double           scale;

    src = rgba_to_cairo_surface(rgba, width, height);
    if (src == NULL)
        return NULL;

    if (max_dim <= 0 || (width <= max_dim && height <= max_dim))
        return src;

    scale = (double)max_dim / (double)MAX(width, height);
    dw = MAX(1, (int)((double)width * scale + 0.5));
    dh = MAX(1, (int)((double)height * scale + 0.5));

    dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(dst);
        return src;
    }

    cr = cairo_create(dst);
    cairo_scale(cr, (double)dw / (double)width, (double)dh / (double)height);
    cairo_set_source_surface(cr, src, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_destroy(cr);

    cairo_surface_destroy(src);

    return dst;
}

/* 一维盒式模糊（就地），dir = 0 横向 / 1 纵向 */
static void box_blur_pass(guchar *data, int w, int h, int stride, int radius, int dir)
{
    if (radius < 1)
        return;

    {
        int     len = (dir == 0) ? w : h;
        int     lines = (dir == 0) ? h : w;
        int     step = (dir == 0) ? 4 : stride;
        int     line_step = (dir == 0) ? stride : 4;
        guchar *copy = g_malloc((size_t)len * 4);

        for (int line = 0; line < lines; line++) {
            guchar *row = data + (size_t)line * line_step;
            int     a[4] = { 0, 0, 0, 0 };
            int     count = 0;

            if (dir == 0) {
                memcpy(copy, row, (size_t)len * 4);
            } else {
                /* 纵向：按 stride 把一列像素收集到连续内存里。
                 * 原来这里直接 memcpy，纵向 pass 读的其实是第一行的数据，
                 * 会在整屏拉出斜向条纹。 */
                for (int i = 0; i < len; i++)
                    memcpy(copy + (size_t)i * 4, row + (size_t)i * stride, 4);
            }

            /* 滑动窗口：先把左边 radius 个样本加进来 */
            for (int i = 0; i <= radius && i < len; i++) {
                for (int c = 0; c < 4; c++)
                    a[c] += copy[i * 4 + c];
                count++;
            }

            for (int i = 0; i < len; i++) {
                for (int c = 0; c < 4; c++) {
                    row[(size_t)i * step + c] = (guchar)(a[c] / count);
                }

                /* 窗口右移：去掉最左、加上最右 */
                if (i - radius >= 0) {
                    for (int c = 0; c < 4; c++)
                        a[c] -= copy[(size_t)(i - radius) * 4 + c];
                    count--;
                }

                if (i + radius + 1 < len) {
                    for (int c = 0; c < 4; c++)
                        a[c] += copy[(size_t)(i + radius + 1) * 4 + c];
                    count++;
                }
            }
        }

        g_free(copy);
    }
}

void surface_box_blur(cairo_surface_t *surface, int radius, int passes)
{
    int      w, h, stride;
    guchar  *data;

    if (surface == NULL || cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32)
        return;

    w      = cairo_image_surface_get_width(surface);
    h      = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);

    if (w <= 0 || h <= 0)
        return;

    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);

    for (int i = 0; i < passes; i++) {
        box_blur_pass(data, w, h, stride, radius, 0);
        box_blur_pass(data, w, h, stride, radius, 1);
    }

    cairo_surface_mark_dirty(surface);
}
