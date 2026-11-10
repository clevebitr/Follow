/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#include <math.h>

#include "theme.h"

const Rgba col_text  = { 0.902, 0.902, 0.910, 0.94 };
const Rgba col_dim   = { 0.902, 0.902, 0.910, 0.45 };
const Rgba col_hint  = { 0.902, 0.902, 0.910, 0.34 };
const Rgba col_faint = { 0.902, 0.902, 0.910, 0.14 };
const Rgba col_card  = { 0.020, 0.020, 0.025, 0.62 };
const Rgba col_warn  = { 0.950, 0.720, 0.450, 0.85 };
const Rgba col_error = { 0.950, 0.550, 0.550, 0.85 };

static PangoLayout *build_layout(cairo_t *cr, const char *text, const TextStyle *style)
{
    PangoLayout          *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc   = pango_font_description_new();

    pango_font_description_set_family(desc, style->family);
    pango_font_description_set_absolute_size(desc, style->px * PANGO_SCALE);
    pango_font_description_set_weight(desc, style->weight);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    if (style->letter_spacing > 0.1) {
        PangoAttrList *attrs = pango_attr_list_new();

        pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(
            (int)(style->letter_spacing * PANGO_SCALE)));
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }

    pango_layout_set_text(layout, text, -1);

    return layout;
}

/* 单行 / 多行的公共设置。max_width <= 0 表示不限宽也不换行。 */
static PangoLayout *build_block(cairo_t *cr, const char *text, const TextStyle *style,
                                double max_width, int align, int max_lines)
{
    PangoLayout *layout = build_layout(cr, text, style);

    if (max_width > 8.0) {
        pango_layout_set_width(layout, (int)(max_width * PANGO_SCALE));
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_alignment(layout,
                                   align == TEXT_CENTER ? PANGO_ALIGN_CENTER
                                                        : PANGO_ALIGN_LEFT);
    }

    if (max_lines > 0) {
        pango_layout_set_height(layout, -max_lines);   /* 负数 = 最多几行 */
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    }

    return layout;
}

double text_width(cairo_t *cr, const char *text, const TextStyle *style)
{
    PangoLayout *layout;
    int          width = 0;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return 0.0;

    layout = build_layout(cr, text, style);
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);

    return (double)width;
}

double text_height(cairo_t *cr, const char *text, const TextStyle *style)
{
    PangoLayout *layout;
    int          height = 0;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return 0.0;

    layout = build_layout(cr, text, style);
    pango_layout_get_pixel_size(layout, NULL, &height);
    g_object_unref(layout);

    return (double)height;
}

void text_draw(cairo_t *cr, const char *text, const TextStyle *style,
               double x, double y, int align, double max_width)
{
    PangoLayout *layout;
    int          width = 0;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return;

    if (max_width > 8.0) {
        /* 单行：放不下就省略号，绝不折行（胶囊、曲名这类都用它） */
        layout = build_block(cr, text, style, max_width, align, 1);
        if (align == TEXT_CENTER)
            x -= max_width * 0.5;
    } else {
        layout = build_layout(cr, text, style);
        pango_layout_get_pixel_size(layout, &width, NULL);

        if (align == TEXT_CENTER)
            x -= (double)width * 0.5;
    }

    color_set(cr, style->color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

double text_block_height(cairo_t *cr, const char *text, const TextStyle *style,
                         double max_width, int max_lines)
{
    PangoLayout *layout;
    int          height = 0;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return 0.0;

    layout = build_block(cr, text, style, max_width, TEXT_CENTER, max_lines);
    pango_layout_get_pixel_size(layout, NULL, &height);
    g_object_unref(layout);

    return (double)height;
}

double text_draw_block(cairo_t *cr, const char *text, const TextStyle *style,
                       double x, double y, int align, double max_width, int max_lines)
{
    PangoLayout *layout;
    int          height = 0;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return 0.0;

    layout = build_block(cr, text, style, max_width, align, max_lines);
    pango_layout_get_pixel_size(layout, NULL, &height);

    /* 限宽时布局宽度就是 max_width，居中要按这个宽度算 */
    if (max_width > 8.0 && align == TEXT_CENTER)
        x -= max_width * 0.5;

    color_set(cr, style->color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    return (double)height;
}

void path_round_rect(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    double r = clampd(radius, 0.0, MIN(w, h) * 0.5);

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

void card_draw(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    if (w <= 1.0 || h <= 1.0)
        return;

    color_set(cr, col_card);
    path_round_rect(cr, x, y, w, h, radius);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

void color_set(cairo_t *cr, Rgba c)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
