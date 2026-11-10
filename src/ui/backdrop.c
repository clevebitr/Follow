/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#include "backdrop.h"

#include <math.h>

#include <gtk/gtk.h>

#include "cover.h"
#include "texture.h"
#include "theme.h"
#include "ui.h"

#define BG_WIDTH      256      /* 小纹理的宽度，放大交给 GPU */
#define BG_BLUR_DIM   160      /* 先把封面缩到这个长边再模糊 */
#define BG_FRAME_SEC  0.0333   /* 30fps 一帧推进的相位 */

static cairo_surface_t *blur_surface(cairo_surface_t *src, int max_dim)
{
    cairo_surface_t *cur;
    int              w, h;

    if (src == NULL)
        return NULL;

    cur = cairo_surface_reference(src);
    w   = cairo_image_surface_get_width(cur);
    h   = cairo_image_surface_get_height(cur);

    while (w > max_dim || h > max_dim) {
        int              nw   = MAX(1, w / 2);
        int              nh   = MAX(1, h / 2);
        cairo_surface_t *next = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw, nh);
        cairo_t         *cr;

        if (cairo_surface_status(next) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(next);
            break;
        }

        cr = cairo_create(next);
        cairo_scale(cr, (double)nw / (double)w, (double)nh / (double)h);
        cairo_set_source_surface(cr, cur, 0.0, 0.0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_destroy(cr);

        cairo_surface_destroy(cur);
        cur = next;
        w   = nw;
        h   = nh;
    }

    return cur;
}

/* 小纹理的尺寸 */
static void bg_small_size(int width, int height, int *out_w, int *out_h)
{
    int h;

    if (width <= 0 || height <= 0) {
        *out_w = BG_WIDTH;
        *out_h = 180;
        return;
    }

    h = (int)((double)BG_WIDTH * (double)height / (double)width + 0.5);

    *out_w = BG_WIDTH;
    *out_h = CLAMP(h, 96, 640);
}

/* 模糊封面缩略图：只在换封面 / 宽高比变化时重建 */
static void build_base(int width, int height)
{
    int      sw, sh;
    cairo_t *cr;

    bg_small_size(width, height, &sw, &sh);

    if (app.bg_base != NULL && app.bg_base_w == sw && app.bg_base_h == sh)
        return;

    g_clear_pointer(&app.bg_base, cairo_surface_destroy);

    app.bg_base   = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    app.bg_base_w = sw;
    app.bg_base_h = sh;

    if (cairo_surface_status(app.bg_base) != CAIRO_STATUS_SUCCESS) {
        g_clear_pointer(&app.bg_base, cairo_surface_destroy);
        app.bg_base_w = app.bg_base_h = 0;
        return;
    }

    cr = cairo_create(app.bg_base);
    cairo_set_source_rgba(cr, 0.035, 0.035, 0.040, 1.0);
    cairo_paint(cr);

    if (cover_base() != NULL) {
        cairo_surface_t *small = blur_surface(cover_base(), BG_BLUR_DIM);

        if (small != NULL) {
            double iw = (double)cairo_image_surface_get_width(small);
            double ih = (double)cairo_image_surface_get_height(small);

            if (iw > 0.0 && ih > 0.0) {
                double scale = MAX((double)sw / iw, (double)sh / ih) * 1.12;

                surface_box_blur(small, MAX(2, (int)(iw / 18.0)), 3);

                cairo_save(cr);
                cairo_translate(cr, ((double)sw - iw * scale) * 0.5,
                                ((double)sh - ih * scale) * 0.5);
                cairo_scale(cr, scale, scale);
                cairo_set_source_surface(cr, small, 0.0, 0.0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
                cairo_paint(cr);
                cairo_restore(cr);
            }

            cairo_surface_destroy(small);
        }
    }

    cairo_destroy(cr);
}

/* cairo 的 ARGB32（小端 = BGRA，预乘）正好等于 GDK_MEMORY_B8G8R8A8_PREMULTIPLIED */
static GdkTexture *texture_from_surface(cairo_surface_t *surface)
{
    int         w, h, stride;
    guchar     *data;
    GBytes     *bytes;
    GdkTexture *texture;

    if (surface == NULL)
        return NULL;

    w      = cairo_image_surface_get_width(surface);
    h      = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);
    data   = cairo_image_surface_get_data(surface);

    if (w <= 0 || h <= 0 || data == NULL)
        return NULL;

    cairo_surface_flush(surface);

    bytes   = g_bytes_new(data, (gsize)stride * (gsize)h);
    texture = gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED,
                                     bytes, (gsize)stride);
    g_bytes_unref(bytes);

    return texture;
}

/* RGB → HSV 平移色相 → RGB */
static void hue_shift(double *r, double *g, double *b, double degrees)
{
    double max = MAX(*r, MAX(*g, *b));
    double min = MIN(*r, MIN(*g, *b));
    double d   = max - min;
    double h, s, v, c, x, m;

    if (d <= 1e-6)
        return;

    v = max;
    s = d / max;

    if (max == *r)      h = fmod((*g - *b) / d, 6.0);
    else if (max == *g) h = (*b - *r) / d + 2.0;
    else                h = (*r - *g) / d + 4.0;

    h = fmod(h + degrees / 60.0, 6.0);
    if (h < 0.0)
        h += 6.0;

    c = v * s;
    x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
    m = v - c;

    if (h < 1.0)      { *r = c; *g = x; *b = 0; }
    else if (h < 2.0) { *r = x; *g = c; *b = 0; }
    else if (h < 3.0) { *r = 0; *g = c; *b = x; }
    else if (h < 4.0) { *r = 0; *g = x; *b = c; }
    else if (h < 5.0) { *r = x; *g = 0; *b = c; }
    else              { *r = c; *g = 0; *b = x; }

    *r += m;
    *g += m;
    *b += m;
}

/* 三团渐变：位置 / 半径 / 透明度按不同频率缓慢漂移 */
static void render_frame(void)
{
    struct Blob {
        double bx, by, speed, phase, radius, alpha, r, g, b;
    } blobs[3];
    int      sw, sh;
    cairo_t *cr;
    Palette  pal;
    double   p;

    if (app.bg_frame == NULL)
        return;

    sw = cairo_image_surface_get_width(app.bg_frame);
    sh = cairo_image_surface_get_height(app.bg_frame);

    /* 退潮进度：先算出来，种子色和底图都用同一个值 */
    p = app.bg_prev != NULL ? fx_t(&app.fx_bg) : 1.0;

    pal = (app.bg_prev != NULL && p < 1.0)
        ? palette_mix(&app.pal_bg_prev, &app.palette, p)
        : app.palette;

    blobs[0] = (struct Blob){ 0.76, 0.18, 0.50, 0.0, 1.15, 0.58,
                              pal.seed_r, pal.seed_g, pal.seed_b };
    blobs[1] = (struct Blob){ 0.14, 0.68, 0.37, 2.1, 1.00, 0.46,
                              pal.alt_r, pal.alt_g, pal.alt_b };
    blobs[2] = (struct Blob){ 0.55, 1.00, 0.29, 4.2, 0.95, 0.44,
                              pal.deep_r, pal.deep_g, pal.deep_b };

    cr = cairo_create(app.bg_frame);

    /* 兜底近黑底 */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.035, 0.035, 0.040, 1.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* 背景退潮：新底图先铺满，旧底图整块左移退出，它的右边缘带着一条渐隐带滑走 */
    if (app.bg_base != NULL) {
        cairo_set_source_surface(cr, app.bg_base, 0.0, 0.0);
        cairo_paint(cr);
    }

    if (app.bg_prev != NULL && p < 1.0) {
        double          seam = (1.0 - p) * (double)sw;
        double          band = (double)sw * 0.26;
        cairo_pattern_t *mask = cairo_pattern_create_linear(0.0, 0.0, (double)sw, 0.0);

        cairo_pattern_add_color_stop_rgba(mask, 0.0, 0.0, 0.0, 0.0, 1.0);
        cairo_pattern_add_color_stop_rgba(mask, clampd((seam - band) / (double)sw, 0.0, 1.0),
                                          0.0, 0.0, 0.0, 1.0);
        cairo_pattern_add_color_stop_rgba(mask, clampd(seam / (double)sw, 0.0, 1.0),
                                          0.0, 0.0, 0.0, 0.0);
        cairo_pattern_add_color_stop_rgba(mask, 1.0, 0.0, 0.0, 0.0, 0.0);

        cairo_set_source_surface(cr, app.bg_prev, -p * (double)sw, 0.0);
        cairo_mask(cr, mask);
        cairo_pattern_destroy(mask);
    } else {
        g_clear_pointer(&app.bg_prev, cairo_surface_destroy);
    }

    for (int i = 0; i < 3; i++) {
        double           a     = app.bg_phase * blobs[i].speed + blobs[i].phase;
        double           x     = (blobs[i].bx + 0.30 * sin(a)) * (double)sw;
        double           y     = (blobs[i].by + 0.24 * cos(a * 0.83)) * (double)sh;
        double           r     = blobs[i].radius * (double)MAX(sw, sh)
                                 * (1.0 + 0.22 * sin(a * 0.7));
        double           alpha = blobs[i].alpha * (1.0 + 0.28 * sin(a * 0.55 + 1.0));
        double           br = blobs[i].r, bg = blobs[i].g, bb = blobs[i].b;
        cairo_pattern_t *p;

        hue_shift(&br, &bg, &bb, 14.0 * sin(a * 0.45 + (double)i));

        p = cairo_pattern_create_radial(x, y, 0.0, x, y, r);
        cairo_pattern_add_color_stop_rgba(p, 0.00, br, bg, bb, alpha);
        cairo_pattern_add_color_stop_rgba(p, 0.45, br, bg, bb, alpha * 0.42);
        cairo_pattern_add_color_stop_rgba(p, 1.00, br, bg, bb, 0.0);

        cairo_set_source(cr, p);
        cairo_paint(cr);
        cairo_pattern_destroy(p);
    }

    /* 压暗，保证白字 / 二维码可读 */
    {
        cairo_pattern_t *scrim = cairo_pattern_create_linear(0.0, 0.0, 0.0, (double)sh);

        cairo_pattern_add_color_stop_rgba(scrim, 0.0, 0.0, 0.0, 0.0, 0.55);
        cairo_pattern_add_color_stop_rgba(scrim, 0.55, 0.0, 0.0, 0.0, 0.70);
        cairo_pattern_add_color_stop_rgba(scrim, 1.0, 0.0, 0.0, 0.0, 0.88);

        cairo_set_source(cr, scrim);
        cairo_paint(cr);
        cairo_pattern_destroy(scrim);
    }

    cairo_destroy(cr);
}

void backdrop_swap(void)
{
    /* 换封面：把当前底图和种子色留作退场用，并开始退场过渡 */
    g_clear_pointer(&app.bg_prev, cairo_surface_destroy);

    if (app.bg_base != NULL) {
        app.bg_prev     = cairo_surface_reference(app.bg_base);
        app.pal_bg_prev = app.palette;
        fx_begin(&app.fx_bg, 1.10);
    }
}

void backdrop_invalidate(void)
{
    g_clear_pointer(&app.bg_base, cairo_surface_destroy);
    app.bg_base_w = 0;
    app.bg_base_h = 0;
}

void backdrop_drop(void)
{
    backdrop_invalidate();
    g_clear_pointer(&app.bg_prev, cairo_surface_destroy);
    g_clear_pointer(&app.bg_frame, cairo_surface_destroy);
    fx_stop(&app.fx_bg);
}

void backdrop_push(int width, int height)
{
    int sw, sh;

    bg_small_size(width, height, &sw, &sh);

    if (app.bg_frame == NULL ||
        cairo_image_surface_get_width(app.bg_frame) != sw ||
        cairo_image_surface_get_height(app.bg_frame) != sh) {
        g_clear_pointer(&app.bg_frame, cairo_surface_destroy);

        app.bg_frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);

        if (cairo_surface_status(app.bg_frame) != CAIRO_STATUS_SUCCESS) {
            g_clear_pointer(&app.bg_frame, cairo_surface_destroy);
            return;
        }
    }

    build_base(width, height);
    render_frame();

    if (app.bg_picture != NULL) {
        GdkTexture *texture = texture_from_surface(app.bg_frame);

        if (texture != NULL) {
            gtk_picture_set_paintable(GTK_PICTURE(app.bg_picture), GDK_PAINTABLE(texture));
            g_object_unref(texture);
        }
    }
}

gboolean backdrop_tick(gpointer data)
{
    int width  = gtk_widget_get_width(app.window);
    int height = gtk_widget_get_height(app.window);

    (void)data;

    if (width <= 0 || height <= 0 || !gtk_widget_get_mapped(app.window))
        return G_SOURCE_CONTINUE;

    app.bg_phase += BG_FRAME_SEC;
    backdrop_push(width, height);

    return G_SOURCE_CONTINUE;
}
