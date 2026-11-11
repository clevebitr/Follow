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

#include <math.h>
#include <string.h>

#include "theme.h"
#include "texture.h"
#include "uidbg.h"

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

    if (uidbg_enabled()) {
        int tw = 0, th = 0;

        pango_layout_get_pixel_size(layout, &tw, &th);
        uidbg_record_text(x, y, max_width > 8.0 ? max_width : (double)tw, (double)th,
                          style->px, style->color, text);
    }

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

/* 自动折行：交给 Pango 排一次（限宽、不限行数、不省略），
 * 再把每个视觉行的字节区间和纵向位置抄出来。
 * 之后再按行单独画字形遮罩，长句就不会被“最多几行 + 省略号”截掉。 */
TextRow *text_wrap(cairo_t *cr, const char *text, const TextStyle *style,
                   double max_width, int *out_n, double *out_h)
{
    PangoLayout     *layout;
    const GSList    *iter;
    TextRow         *rows = NULL;
    int              n = 0, cap = 0, block_h = 0;
    double           acc = 0.0;

    if (out_n != NULL)
        *out_n = 0;
    if (out_h != NULL)
        *out_h = 0.0;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return NULL;

    layout = build_block(cr, text, style, max_width, TEXT_LEFT, 0);
    pango_layout_get_pixel_size(layout, NULL, &block_h);

    for (iter = pango_layout_get_lines_readonly(layout); iter != NULL; iter = iter->next) {
        PangoLayoutLine *line = iter->data;
        PangoRectangle    ink, logical;
        double            y, h;

        if (line == NULL || line->length <= 0)
            continue;

        /* readonly 行只读，不改它，拿一下行内文字的范围和行高。
         * 注意 pango_layout_line_get_extents() 的纵向是相对这一行自己的原点，
         * 不是相对整个 layout，所以偏移得自己累加。 */
        pango_layout_line_get_extents(line, &ink, &logical);
        y = acc;
        h = (double)logical.height / PANGO_SCALE;

        if (h <= 0.0)
            h = style->px * 1.45;

        if (n == cap) {
            cap  = cap == 0 ? 8 : cap * 2;
            rows = g_renew(TextRow, rows, (gsize)cap);
        }

        rows[n].start = (guint)line->start_index;
        rows[n].len   = (guint)line->length;
        rows[n].y     = y;
        rows[n].h     = h;
        n++;

        acc += h;
    }

    g_object_unref(layout);

    if (n == 0) {
        g_free(rows);
        return NULL;
    }

    if (out_n != NULL)
        *out_n = n;
    if (out_h != NULL)
        *out_h = acc > 0.0 ? acc : (double)block_h;

    return rows;
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

    if (uidbg_enabled())
        uidbg_record_text(x, y, max_width > 8.0 ? max_width : 0.0, (double)height,
                          style->px, style->color, text);

    g_object_unref(layout);

    return (double)height;
}

/* ---------- 文字 => GPU 遮罩 ---------- */

cairo_surface_t *text_mask_surface(const char *text, const TextStyle *style,
                                   double max_width, int align, int max_lines,
                                   int pad, int *out_w, int *out_h)
{
    PangoLayout     *layout;
    cairo_surface_t *scratch;
    cairo_surface_t *mask;
    cairo_t         *cr;
    int              tw = 0, th = 0;
    double           x = pad;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return NULL;

    /* Pango 需要一个 cairo_t 才能建 layout；量尺寸用 1x1 的临时面 */
    scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cr      = cairo_create(scratch);
    layout  = build_block(cr, text, style, max_width, align, max_lines);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_destroy(cr);
    cairo_surface_destroy(scratch);

    if (tw <= 0 || th <= 0) {
        g_object_unref(layout);
        return NULL;
    }

    mask = cairo_image_surface_create(CAIRO_FORMAT_A8, tw + pad * 2, th + pad * 2);

    if (cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(mask);
        g_object_unref(layout);
        return NULL;
    }

    cr = cairo_create(mask);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);

    cairo_move_to(cr, x, pad);
    pango_cairo_show_layout(cr, layout);

    cairo_destroy(cr);
    g_object_unref(layout);

    if (out_w != NULL)
        *out_w = tw;
    if (out_h != NULL)
        *out_h = th;

    return mask;
}

/* ---------- 文字 => GPU 遮罩（字形 + 逐字时间坐标） ---------- */

/* 把 utf8 字符在整句里的位置（0..255）写成灰度属性，然后渲染出来。
 * 结果是一张和字形遮罩逐像素对齐的"时间坐标图"：
 *   GPU 上拿它和播放进度比较，就能做出 AMLL 那种从左到右逐个字亮起的效果。
 * base / total 是这段文字在整句里的起始字序号和整句字数（折行后仍然连续）。 */
static void build_order_attrs(PangoLayout *layout, const char *text,
                              const guint8 *order, int n_order)
{
    PangoAttrList *attrs = pango_layout_get_attributes(layout);
    gsize          n_chars = 0;
    const gchar   *p;

    if (attrs == NULL) {
        attrs = pango_attr_list_new();
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);       /* 布局自己持有一份 */
    }

    for (p = text; *p != '\0'; p = g_utf8_next_char(p))
        n_chars++;

    if (n_chars == 0)
        return;

    {
        gsize i = 0;

        for (p = text; *p != '\0' && i < n_chars; p = g_utf8_next_char(p), i++) {
            gsize          b0 = (gsize)(p - text);
            gsize          b1 = (gsize)(g_utf8_next_char(p) - text);
            guint8         v8 = (order != NULL && i < (gsize)n_order) ? order[i] : 0;
            guint16        v  = (guint16)v8 * 257u;
            PangoAttribute *attr;

            if (b1 > G_MAXUINT)
                break;

            attr = pango_attr_foreground_new(v, v, v);
            attr->start_index = (guint)b0;
            attr->end_index   = (guint)b1;
            pango_attr_list_insert(attrs, attr);
        }
    }
}

gboolean text_mask_pair(const char *text, const TextStyle *style, int pad,
                        const guint8 *order, int n_order,
                        cairo_surface_t **out_glyph, cairo_surface_t **out_order,
                        int *out_w, int *out_h)
{
    PangoLayout     *layout;
    cairo_surface_t *scratch;
    cairo_surface_t *glyph  = NULL;
    cairo_surface_t *order_surf = NULL;
    cairo_surface_t *argb   = NULL;
    cairo_t         *cr;
    int              tw = 0, th = 0, sw, sh;
    double           x = pad;

    if (out_glyph != NULL)
        *out_glyph = NULL;
    if (out_order != NULL)
        *out_order = NULL;

    if (text == NULL || *text == '\0' || style->px <= 0.0)
        return FALSE;

    scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cr      = cairo_create(scratch);
    /* 单行：一行已经由 text_wrap 折好，这里不再限宽 / 省略 */
    layout  = build_layout(cr, text, style);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_destroy(cr);
    cairo_surface_destroy(scratch);

    if (tw <= 0 || th <= 0) {
        g_object_unref(layout);
        return FALSE;
    }

    sw = tw + pad * 2;
    sh = th + pad * 2;

    /* 逐字灰阶必须在第一次排版使用之前塞进去：Pango 的行缓存会记住属性，
     * 事后改属性列表不会生效（渲染出来全是底色）。 */
    if (out_order != NULL) {
        build_order_attrs(layout, text, order, n_order);
        pango_layout_context_changed(layout);
    }

    /* 1) 字形遮罩 */
    glyph = cairo_image_surface_create(CAIRO_FORMAT_A8, sw, sh);

    if (cairo_surface_status(glyph) != CAIRO_STATUS_SUCCESS)
        goto fail;

    cr = cairo_create(glyph);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_move_to(cr, x, pad);
    pango_cairo_show_layout(cr, layout);
    cairo_destroy(cr);

    /* 2) 逐字时间坐标：每个字用不同的灰阶画出来，再抽出 R 通道 */
    if (out_order != NULL) {
        argb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);

        if (cairo_surface_status(argb) != CAIRO_STATUS_SUCCESS)
            goto fail;

        cr = cairo_create(argb);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
        cairo_paint(cr);                       /* 底色 = 0（没有字的地方当作"已唱完"） */
        cairo_move_to(cr, x, pad);
        pango_cairo_show_layout(cr, layout);
        cairo_destroy(cr);

        order_surf = cairo_image_surface_create(CAIRO_FORMAT_A8, sw, sh);

        if (cairo_surface_status(order_surf) != CAIRO_STATUS_SUCCESS)
            goto fail;

        cairo_surface_flush(argb);

        {
            const guchar *src    = cairo_image_surface_get_data(argb);
            int           stride = cairo_image_surface_get_stride(argb);
            guchar       *dst;
            int           dstride;

            cairo_surface_flush(order_surf);
            dst     = cairo_image_surface_get_data(order_surf);
            dstride = cairo_image_surface_get_stride(order_surf);

            for (int y = 0; y < sh; y++) {
                const guint32 *s = (const guint32 *)(src + (gsize)y * stride);
                guchar        *d = dst + (gsize)y * dstride;

                for (int xx = 0; xx < sw; xx++)
                    d[xx] = (guchar)((s[xx] >> 16) & 0xff);   /* 小端 ARGB32 的 R */
            }

            cairo_surface_mark_dirty(order_surf);
        }

        cairo_surface_destroy(argb);
        argb = NULL;
    }

    g_object_unref(layout);

    if (out_glyph != NULL)
        *out_glyph = glyph;
    else
        cairo_surface_destroy(glyph);

    if (out_order != NULL)
        *out_order = order_surf;
    else if (order_surf != NULL)
        cairo_surface_destroy(order_surf);

    if (out_w != NULL)
        *out_w = sw;
    if (out_h != NULL)
        *out_h = sh;

    return TRUE;

fail:
    if (glyph != NULL)
        cairo_surface_destroy(glyph);

    if (order_surf != NULL)
        cairo_surface_destroy(order_surf);

    if (argb != NULL)
        cairo_surface_destroy(argb);

    g_object_unref(layout);
    return FALSE;
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

    if (uidbg_enabled())
        uidbg_record_rect(x, y, w, h, col_card, "card");

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
