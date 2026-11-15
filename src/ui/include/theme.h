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

#ifndef FOLLOW_THEME_H
#define FOLLOW_THEME_H

#include <cairo.h>
#include <pango/pangocairo.h>

/* 字体：正文与时钟统一使用等宽字体。 */
#define FONT_MONO "Iosevka,Noto Sans SC,Sarasa Mono SC,Cascadia Mono,Microsoft YaHei UI,monospace"
#define FONT_CLOCK FONT_MONO

typedef struct {
    double r, g, b, a;
} Rgba;

#define RGBA(r, g, b, a)   (Rgba){ (r), (g), (b), (a) }
#define WHITE(a)           RGBA(0.902, 0.902, 0.910, (a))

extern const Rgba col_text;
extern const Rgba col_dim;
extern const Rgba col_hint;
extern const Rgba col_faint;
extern const Rgba col_card;
extern const Rgba col_warn;
extern const Rgba col_error;

typedef struct {
    const char  *family;
    double       px;
    PangoWeight  weight;
    double       letter_spacing;
    Rgba         color;
} TextStyle;

#define TEXT_STYLE(family, px, weight, spacing, color) \
    (TextStyle){ (family), (px), (weight), (spacing), (color) }

enum { TEXT_LEFT = 0, TEXT_CENTER };

double text_width(cairo_t *cr, const char *text, const TextStyle *style);
double text_height(cairo_t *cr, const char *text, const TextStyle *style);

void text_draw(cairo_t *cr, const char *text, const TextStyle *style,
               double x, double y, int align, double max_width);

double text_block_height(cairo_t *cr, const char *text, const TextStyle *style,
                         double max_width, int max_lines);

/* 墨迹相对排版框的上下边界。 */
double text_ink_top(cairo_t *cr, const char *text, const TextStyle *style);
double text_ink_bottom(cairo_t *cr, const char *text, const TextStyle *style);

/* 生成 A8 字形遮罩（GPU 纹理）。 */
cairo_surface_t *text_mask_surface(const char *text, const TextStyle *style,
                                   double max_width, int align, int max_lines,
                                   int pad, int *out_w, int *out_h);

/* 按宽度折行，返回每行区间与位置。 */
typedef struct {
    guint  start;
    guint  len;
    double y;
    double h;
} TextRow;

/* 返回行数组（g_free 释放）。 */
TextRow *text_wrap(cairo_t *cr, const char *text, const TextStyle *style,
                   double max_width, int *out_n, double *out_h);

/* 生成字形与逐字时间两张对齐遮罩。 */
gboolean text_mask_pair(const char *text, const TextStyle *style, int pad,
                        const guint8 *order, int n_order,
                        cairo_surface_t **out_glyph, cairo_surface_t **out_order,
                        int *out_w, int *out_h);

double text_draw_block(cairo_t *cr, const char *text, const TextStyle *style,
                       double x, double y, int align, double max_width, int max_lines);

void card_draw(cairo_t *cr, double x, double y, double w, double h, double radius);

void path_round_rect(cairo_t *cr, double x, double y, double w, double h, double radius);

void color_set(cairo_t *cr, Rgba c);

double clampd(double v, double lo, double hi);

#endif
