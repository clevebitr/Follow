/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#include "qrcard.h"

#include <math.h>
#include <string.h>

#include <qrencode.h>

#include "platform.h"
#include "theme.h"
#include "ui.h"

/* 二维码的底色 */
#define QR_BG_R 0.039
#define QR_BG_G 0.039
#define QR_BG_B 0.039
#define QR_INK_ALPHA 0.95

/* 槽的增删 */

static void qr_slot_clear(QrSlot *slot)
{
    /* free 完必须置 NULL —— qr_slot_set() 也会先 clear 一次 */
    g_clear_pointer(&slot->text, g_free);
    g_clear_pointer(&slot->label, g_free);

    if (slot->code != NULL) {
        QRcode_free(slot->code);
        slot->code = NULL;
    }

    g_clear_pointer(&slot->surface, cairo_surface_destroy);

    slot->pixels    = 0;
    slot->platform  = PLATFORM_UNKNOWN;
    slot->with_icon = FALSE;
}

/* "https://www.github.com/xxx/" → "github.com/xxx" */
static gchar *short_url(const char *url)
{
    const char *p = url;
    gchar      *out;
    gsize       len;

    if (url == NULL)
        return NULL;

    if (g_str_has_prefix(p, "https://"))     p += 8;
    else if (g_str_has_prefix(p, "http://")) p += 7;

    if (g_str_has_prefix(p, "www.")) p += 4;

    out = g_strdup(p);
    len = strlen(out);

    while (len > 0 && out[len - 1] == '/')
        out[--len] = '\0';

    return out;
}

/* "https://www.github.com/x/y" → "github.com" */
static gchar *url_host(const char *url)
{
    const char *p = url;
    const char *end;

    if (url == NULL)
        return NULL;

    if (g_str_has_prefix(p, "https://"))     p += 8;
    else if (g_str_has_prefix(p, "http://")) p += 7;

    if (g_str_has_prefix(p, "www.")) p += 4;

    end = p;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#')
        end++;

    return g_strndup(p, (gsize)(end - p));
}

static void qr_slot_set(QrSlot *slot, const char *text, const char *label,
                        Platform platform, gboolean with_icon)
{
    qr_slot_clear(slot);

    if (text == NULL || *text == '\0')
        return;

    slot->text      = g_strdup(text);
    slot->label     = label != NULL ? g_strdup(label) : short_url(text);
    slot->platform  = platform;
    slot->with_icon = with_icon;
    slot->code      = QRcode_encodeString(text, 0, QR_ECLEVEL_M, QR_MODE_8, 1);

    if (slot->code == NULL)
        g_warning("qrcode: cannot encode %s", text);
}

/* 渲染 */

/* 预渲染成边长为 size 的正方形. 模块大小是分数，但每条边都对到像素 */
static cairo_surface_t *qr_slot_surface(QrSlot *slot, double size)
{
    int      n, side;
    double   m;
    cairo_t *cr;

    if (slot->code == NULL || slot->code->width <= 0)
        return NULL;

    n    = slot->code->width;
    side = MAX(8, (int)floor(size + 0.5));

    if (slot->surface != NULL && slot->pixels == side)
        return slot->surface;

    g_clear_pointer(&slot->surface, cairo_surface_destroy);

    slot->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
    slot->pixels  = side;

    if (cairo_surface_status(slot->surface) != CAIRO_STATUS_SUCCESS) {
        g_clear_pointer(&slot->surface, cairo_surface_destroy);
        slot->pixels = 0;
        return NULL;
    }

    cr = cairo_create(slot->surface);

    cairo_set_source_rgba(cr, QR_BG_R, QR_BG_G, QR_BG_B, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, QR_INK_ALPHA);

    m = (double)side / (double)(n + 2);   /* 含四周一个模块静区 */

    for (int my = 0; my < n; my++) {
        for (int mx = 0; mx < n; mx++) {
            int x0, x1, y0, y1;

            if (!(slot->code->data[my * n + mx] & 1))
                continue;

            x0 = (int)lround((double)(mx + 1) * m);
            x1 = (int)lround((double)(mx + 2) * m);
            y0 = (int)lround((double)(my + 1) * m);
            y1 = (int)lround((double)(my + 2) * m);

            cairo_rectangle(cr, x0, y0, MAX(1, x1 - x0), MAX(1, y1 - y0));
        }
    }

    cairo_fill(cr);
    cairo_destroy(cr);

    return slot->surface;
}

/* 胶囊：高度固定 = h
 *
 *   有官方字标 → 胶囊里直接贴 logo
 *   没有字标   → 退回"品牌色图标 + 文字"，文字垂直水平居中
 *   没有平台   → 只显示文字 */
static void draw_capsule(cairo_t *cr, const char *text, Platform platform,
                         gboolean with_icon, double cx, double y, double h, double max_w)
{
    static const double PAD_RATIO  = 0.34;   /* 左右留白 / 胶囊高 */
    static const double LOGO_RATIO = 0.52;   /* 字标 / 图标高度 / 胶囊高 */
    static const double FONT_RATIO = 0.42;   /* 字号 / 胶囊高 */
    static const double GAP_RATIO  = 0.22;   /* 图标与文字的间距 / 胶囊高 */

    TextStyle style;
    double    pad, logo_h = 0.0, logo_w = 0.0, icon = 0.0, gap = 0.0;
    double    content_w = 0.0, w, x, r = 0.902, g = 0.902, b = 0.910;
    gboolean  has_wordmark = FALSE;
    double    text_h;

    if (h < 6.0 || max_w < 24.0)
        return;

    pad = h * PAD_RATIO;

    /* 官方字标优先 */
    if (with_icon && platform != PLATFORM_UNKNOWN) {
        has_wordmark = platform_wordmark_size(platform, h * LOGO_RATIO,
                                              max_w - pad * 2.0, &logo_w, &logo_h);
        if (has_wordmark)
            content_w = logo_w;
    }

    /* 没有字标：图标 + 文字 */
    if (!has_wordmark) {
        if (text == NULL || *text == '\0')
            return;

        icon = with_icon ? h * LOGO_RATIO : 0.0;
        gap  = with_icon ? h * GAP_RATIO : 0.0;

        style = TEXT_STYLE(FONT_MONO, h * FONT_RATIO, PANGO_WEIGHT_BOLD, 0.0, col_text);

        {
            double allowed = MAX(10.0, max_w - pad * 2.0 - icon - gap);
            double want    = text_width(cr, text, &style);

            content_w = icon + gap + MIN(allowed, want);
        }
    }

    w = MIN(max_w, content_w + pad * 2.0);
    x = cx - w * 0.5;

    /* 深色衬底（高度恒定，垂直居中于 y..y+h） */
    color_set(cr, col_card);
    path_round_rect(cr, x, y, w, h, h * 0.5);
    cairo_fill_preserve(cr);

    if (platform != PLATFORM_UNKNOWN) {
        platform_brand_color(platform, &r, &g, &b);
        cairo_set_source_rgba(cr, r, g, b, 0.55);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    }

    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    if (has_wordmark) {
        double total = logo_w + pad * 2.0;

        platform_draw_wordmark(cr, platform, cx - MIN(total, w) * 0.5 + pad,
                               y + (h - logo_h) * 0.5, logo_w, logo_h);
        return;
    }

    if (with_icon)
        platform_draw_icon(cr, platform, x + pad, y + (h - icon) * 0.5, icon);

    /* 文字水平居中于"图标 + 文字"整体，垂直居中于胶囊 */
    style.color = RGBA(0.902, 0.902, 0.910, 0.86);
    text_h = text_height(cr, text, &style);

    {
        double allowed = MAX(10.0, w - pad * 2.0 - icon - gap);
        double want    = text_width(cr, text, &style);

        text_draw(cr, text, &style, x + pad + icon + gap, y + (h - text_h) * 0.5,
                  TEXT_LEFT, want > allowed ? allowed : 0.0);
    }
}

/* 一个二维码单元格：二维码 + 下方胶囊 */
static void draw_cell(cairo_t *cr, QrSlot *slot, const Layout *L, double cx)
{
    cairo_surface_t *surface;
    double           total;

    if (slot->text == NULL)
        return;

    surface = qr_slot_surface(slot, L->qr_px);
    if (surface == NULL)
        return;

    total = (double)slot->pixels;

    cairo_set_source_surface(cr, surface, cx - total * 0.5, L->qr_y);
    cairo_paint(cr);

    draw_capsule(cr, slot->label, slot->platform, slot->with_icon,
                 cx, L->badge_y, L->badge_h, L->badge_w);
}

/* 对外接口  */

void qrcard_update(const AudioTrack *track)
{
    const APPconfig *cfg  = config_get();
    const gchar     *page = NULL;

    if (track != NULL) {
        if (track->page != NULL && *track->page != '\0' && config_is_remote(track->page))
            page = track->page;
        else if (track->link_remote)
            page = track->link;
    }

    if (page != NULL) {
        Platform platform = PLATFORM_UNKNOWN;

        if (track->platform != NULL)
            platform = platform_from_name(track->platform);
        if (platform == PLATFORM_UNKNOWN)
            platform = platform_from_url(page);

        qr_slot_set(&app.qr[0], page, platform_name(platform), platform, TRUE);
    } else {
        qr_slot_clear(&app.qr[0]);
    }

    /* 自定义链接：本地路径不显示；和曲目页重复也不显示 */
    if (cfg->qrcode_link != NULL && config_is_remote(cfg->qrcode_link) &&
        g_strcmp0(page, cfg->qrcode_link) != 0) {
        g_autofree gchar *host = url_host(cfg->qrcode_link);

        qr_slot_set(&app.qr[1], cfg->qrcode_link,
                    cfg->qrcode_label != NULL ? cfg->qrcode_label : host,
                    PLATFORM_UNKNOWN, FALSE);
    } else {
        qr_slot_clear(&app.qr[1]);
    }

    ui_request_redraw();
}

void qrcard_clear(void)
{
    qr_slot_clear(&app.qr[0]);
    qr_slot_clear(&app.qr[1]);
}

void qrcard_draw(cairo_t *cr, const Layout *L)
{
    double row_w, x0;

    if (L->n_qr <= 0)
        return;

    /* 1 个居中，2 个并排 */
    row_w = (double)L->n_qr * L->cell_w + (double)(L->n_qr - 1) * L->cell_gap;
    x0    = L->col_cx - row_w * 0.5;

    if (app.qr[0].text != NULL)
        draw_cell(cr, &app.qr[0], L,
                  L->n_qr == 2 ? x0 + L->cell_w * 0.5 : L->col_cx);

    if (app.qr[1].text != NULL)
        draw_cell(cr, &app.qr[1], L,
                  L->n_qr == 2 ? x0 + L->cell_w + L->cell_gap + L->cell_w * 0.5
                               : L->col_cx);
}
