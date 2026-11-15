/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#include "details.h"

#include <math.h>
#include <string.h>

#include "config.h"
#include "cover.h"
#include "keymap.h"
#include "metadata.h"
#include "theme.h"
#include "ui.h"

/* 页面状态：只影响绘制，真正的数据在 metadata 状态机里 */
static gboolean g_open;

void details_open(void)
{
    if (g_open)
        return;

    /* 保证打开的第一帧就是当前曲目的数据（切歌后 100ms 内按 I 也不会串曲） */
    {
        gint index = app.track >= 0 ? app.track : 0;

        metadata_load_track(config_track_at(config_get(), (gsize)index));
        cover_apply_metadata();
    }

    g_open = TRUE;
    ui_request_redraw();
}

void details_close(void)
{
    if (!g_open)
        return;

    g_open = FALSE;
    ui_request_redraw();
}

void details_toggle(void)
{
    if (g_open)
        details_close();
    else
        details_open();
}

gboolean details_is_open(void)
{
    return g_open;
}

/* ---------------------------------------------------------------- 工具 --- */

static const gchar *or_dash(const gchar *text)
{
    return (text != NULL && *text != '\0') ? text : "—";
}

/* MB 的录音时长（毫秒）→ "4:12" */
static gchar *format_ms(gint ms)
{
    gint total;

    if (ms <= 0)
        return NULL;

    total = (ms + 500) / 1000;

    return g_strdup_printf("%d:%02d", total / 60, total % 60);
}

static gchar *format_track(gint number, gint count)
{
    if (number <= 0 && count <= 0)
        return NULL;

    if (count <= 0)
        return g_strdup_printf("%d", number);

    return g_strdup_printf("%d / %d", number, count);
}

/* 封面种子色，压到适合做强调色的亮度区间 */
static Rgba accent(double alpha)
{
    const Palette *p = &app.palette;
    double r = p->has ? p->seed_r : 0.62;
    double g = p->has ? p->seed_g : 0.74;
    double b = p->has ? p->seed_b : 0.98;
    double mx = MAX(r, MAX(g, b));

    if (mx > 0.001 && mx < 0.70) {
        double k = 0.70 / mx;

        r *= k; g *= k; b *= k;
    } else if (mx > 0.88) {
        double k = 0.88 / mx;

        r *= k; g *= k; b *= k;
    }

    return RGBA(r, g, b, alpha);
}

static void draw_field(cairo_t *cr, double x, double y, double w,
                       const gchar *label, const gchar *value,
                       double label_px, double value_px, const Rgba *color)
{
    TextStyle ls = TEXT_STYLE(FONT_MONO, label_px, PANGO_WEIGHT_NORMAL,
                             label_px * 0.16, col_hint);
    TextStyle vs = TEXT_STYLE(FONT_MONO, value_px, PANGO_WEIGHT_NORMAL,
                             0.0, color != NULL ? *color : col_text);

    text_draw(cr, label, &ls, x, y, TEXT_LEFT, w);
    text_draw(cr, or_dash(value), &vs, x, y + label_px * 1.5, TEXT_LEFT, w);
}

static double field_height(double label_px, double value_px)
{
    return label_px * 1.5 + value_px * 1.35;
}

/* 单行右对齐（text_draw 只有左 / 居中两种对齐） */
static void text_draw_right(cairo_t *cr, const gchar *text,
                            const TextStyle *style, double right_x, double y)
{
    double w = text_width(cr, text, style);

    text_draw(cr, text, style, right_x - w, y, TEXT_LEFT, 0.0);
}

/* ------------------------------------------------------------ 绘制分块 --- */

typedef struct {
    double pad;
    double head_y;
    double head_px;
    double top;
    double cover_x, cover_y, cover_px;
    double rx, rw;
    double gap;
    double bottom_y;
    double foot_px;
} Geom;

static void draw_header(cairo_t *cr, const Geom *G, double w, const gchar *hint)
{
    TextStyle title = TEXT_STYLE(FONT_MONO, G->head_px, PANGO_WEIGHT_BOLD,
                                 G->head_px * 0.30, col_dim);
    TextStyle right = TEXT_STYLE(FONT_MONO, G->head_px, PANGO_WEIGHT_NORMAL,
                                 G->head_px * 0.16, col_hint);

    text_draw(cr, "TRACK INFO", &title, G->pad, G->head_y, TEXT_LEFT, 400.0);
    text_draw_right(cr, hint != NULL ? hint : "[ I / ESC ]  CLOSE", &right,
                    w - G->pad, G->head_y);
}

/* 右列标题：曲名 / 艺人 / 专辑行。返回下一块可用的 y。 */
static double draw_title(cairo_t *cr, const Geom *G, const gchar *title,
                         const gchar *artist, const gchar *album,
                         const gchar *date, double h)
{
    double    title_px = clampd(h * 0.052, 20.0, 54.0);
    double    artist_px = clampd(title_px * 0.52, 12.0, 26.0);
    double    meta_px = clampd(h * 0.020, 11.0, 17.0);
    TextStyle ts = TEXT_STYLE(FONT_MONO, title_px, PANGO_WEIGHT_NORMAL,
                              title_px * 0.02, col_text);
    TextStyle as = TEXT_STYLE(FONT_MONO, artist_px, PANGO_WEIGHT_NORMAL,
                              artist_px * 0.04, WHITE(0.82));
    TextStyle ms = TEXT_STYLE(FONT_MONO, meta_px, PANGO_WEIGHT_NORMAL,
                              meta_px * 0.12, col_dim);
    double    y = G->top;
    double    th;

    th = text_draw_block(cr, or_dash(title), &ts, G->rx, y, TEXT_LEFT,
                         G->rw, 2);
    y += th + title_px * 0.28;

    text_draw(cr, or_dash(artist), &as, G->rx, y, TEXT_LEFT, G->rw);
    y += artist_px * 1.45;

    {
        g_autofree gchar *line = (album != NULL && *album != '\0')
            ? (date != NULL && *date != '\0'
                ? g_strdup_printf("%s  ·  %s", album, date)
                : g_strdup(album))
            : NULL;

        if (line != NULL) {
            text_draw(cr, line, &ms, G->rx, y, TEXT_LEFT, G->rw);
            y += meta_px * 1.6;
        }
    }

    return y + G->gap * 0.35;
}

static void draw_divider(cairo_t *cr, const Geom *G, double y)
{
    color_set(cr, WHITE(0.08));
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, G->rx, floor(y) + 0.5);
    cairo_line_to(cr, G->rx + G->rw, floor(y) + 0.5);
    cairo_stroke(cr);
}

/* READY：字段网格 */
static void draw_fields(cairo_t *cr, const Geom *G, const TrackMeta *meta,
                        double y, double h)
{
    double label_px = clampd(h * 0.017, 10.0, 15.0);
    double value_px = clampd(h * 0.021, 12.0, 19.0);
    double fh       = field_height(label_px, value_px);
    double col_gap  = G->gap * 0.75;
    double col_w    = (G->rw - col_gap) * 0.5;
    double y2       = y + fh + label_px * 0.6;
    g_autofree gchar *track = format_track(meta->track_number, meta->track_count);
    g_autofree gchar *length = format_ms(meta->duration_ms);
    Rgba    mbid_color = accent(0.92);

    /* 两行 MBID 用整行宽度，UUID 不会被省略 */
    draw_field(cr, G->rx, y, G->rw, "RECORDING MBID",
               meta->recording_mbid, label_px, value_px, &mbid_color);
    draw_field(cr, G->rx, y2, G->rw, "RELEASE MBID",
               meta->release_mbid, label_px, value_px, &mbid_color);
    y2 += fh + label_px * 0.6;

    draw_field(cr, G->rx, y2, col_w, "ALBUM", meta->album,
               label_px, value_px, NULL);
    draw_field(cr, G->rx + col_w + col_gap, y2, col_w, "RELEASED",
               meta->release_date, label_px, value_px, NULL);
    y2 += fh + label_px * 0.6;

    draw_field(cr, G->rx, y2, col_w, "ARTISTS", meta->artist,
               label_px, value_px, NULL);
    draw_field(cr, G->rx + col_w + col_gap, y2, col_w, "COUNTRY",
               meta->country, label_px, value_px, NULL);
    y2 += fh + label_px * 0.6;

    draw_field(cr, G->rx, y2, col_w, "ALBUM ARTIST", meta->album_artist,
               label_px, value_px, NULL);
    draw_field(cr, G->rx + col_w + col_gap, y2, col_w, "LABEL",
               meta->label, label_px, value_px, NULL);
    y2 += fh + label_px * 0.6;

    draw_field(cr, G->rx, y2, col_w, "TRACK", track,
               label_px, value_px, NULL);
    draw_field(cr, G->rx + col_w + col_gap, y2, col_w, "LENGTH",
               length != NULL ? length : "—", label_px, value_px, NULL);
    y2 += fh + label_px * 0.6;

    draw_field(cr, G->rx, y2, col_w, "CATALOG #", meta->catalog_number,
               label_px, value_px, NULL);
    draw_field(cr, G->rx + col_w + col_gap, y2, col_w, "BARCODE",
               meta->barcode, label_px, value_px, NULL);
    y2 += fh + label_px * 0.6;

    draw_field(cr, G->rx, y2, G->rw, "TAGS", meta->tags,
               label_px, value_px, NULL);
}

/* PICK：候选列表 */
static void draw_candidates(cairo_t *cr, const Geom *G, double y, double h,
                            double footer_top)
{
    const GPtrArray *cands = metadata_candidates();
    gint             selected = metadata_selected();
    guint            n = cands != NULL ? cands->len : 0;
    double           label_px = clampd(h * 0.017, 10.0, 15.0);
    double           row_h = clampd(h * 0.070, 34.0, 60.0);
    double           avail = footer_top - y - G->gap * 0.5;
    gint             max_rows = (gint)floor(avail / row_h);
    gint             first;
    TextStyle        caption = TEXT_STYLE(FONT_MONO, label_px, PANGO_WEIGHT_BOLD,
                                          label_px * 0.26, accent(0.85));

    if (max_rows < 2)
        max_rows = 2;

    text_draw(cr, "SELECT THE CORRECT RECORDING", &caption, G->rx, y,
              TEXT_LEFT, G->rw);
    y += label_px * 2.2;

    max_rows = (gint)floor((footer_top - y - G->gap * 0.4) / row_h);
    if (max_rows < 1)
        max_rows = 1;

    first = selected - max_rows / 2;
    if (first > (gint)n - max_rows)
        first = (gint)n - max_rows;
    if (first < 0)
        first = 0;

    for (gint row = 0; row < max_rows && (guint)(first + row) < n; row++) {
        const MetaCandidate *cand = g_ptr_array_index(cands, (guint)(first + row));
        gboolean selected_row = (first + row) == selected;
        double   ry = y + (double)row * row_h;
        double   title_px = clampd(h * 0.023, 13.0, 21.0);
        double   sub_px = clampd(h * 0.017, 10.0, 15.0);
        TextStyle ts = TEXT_STYLE(FONT_MONO, title_px,
                                  selected_row ? PANGO_WEIGHT_BOLD
                                               : PANGO_WEIGHT_NORMAL,
                                  0.0, selected_row ? WHITE(0.96) : col_text);
        TextStyle ss = TEXT_STYLE(FONT_MONO, sub_px, PANGO_WEIGHT_NORMAL,
                                  0.0, selected_row ? WHITE(0.62) : col_dim);
        g_autofree gchar *sub = g_strdup_printf(
            "%s%s%s%s%s",
            or_dash(cand->artist),
            cand->album != NULL ? "  ·  " : "",
            cand->album != NULL ? cand->album : "",
            cand->date != NULL ? "  ·  " : "",
            cand->date != NULL ? cand->date : "");

        if (selected_row) {
            color_set(cr, WHITE(0.055));
            path_round_rect(cr, G->rx, ry, G->rw, row_h - 4.0, 8.0);
            cairo_fill(cr);

            color_set(cr, accent(0.85));
            path_round_rect(cr, G->rx, ry, 2.5, row_h - 4.0, 1.2);
            cairo_fill(cr);
        }

        text_draw(cr, cand->title, &ts, G->rx + 14.0, ry + row_h * 0.14,
                  TEXT_LEFT, G->rw - 100.0);
        text_draw(cr, sub, &ss, G->rx + 14.0, ry + row_h * 0.52,
                  TEXT_LEFT, G->rw - 100.0);

        {
            TextStyle sc = TEXT_STYLE(FONT_MONO, sub_px, PANGO_WEIGHT_NORMAL,
                                     0.0, selected_row ? accent(0.95)
                                                      : col_hint);
            g_autofree gchar *score = g_strdup_printf("%d", cand->score);

            text_draw_right(cr, score, &sc, G->rx + G->rw - 16.0,
                            ry + row_h * 0.30);
        }
    }

    if ((gint)n > max_rows) {
        TextStyle more = TEXT_STYLE(FONT_MONO, label_px, PANGO_WEIGHT_NORMAL,
                                    0.0, col_hint);
        g_autofree gchar *text = g_strdup_printf("%d / %u", selected + 1, n);

        text_draw(cr, text, &more, G->rx + G->rw - 90.0,
                  y + (double)max_rows * row_h + 2.0, TEXT_LEFT, 90.0);
    }
}

/* LOADING / ERROR / IDLE 的状态块 */
static void draw_status(cairo_t *cr, const Geom *G, double y, double h,
                        const gchar *headline, const gchar *detail,
                        const Rgba *color)
{
    double    px = clampd(h * 0.022, 13.0, 21.0);
    double    sub_px = clampd(h * 0.017, 10.0, 15.0);
    TextStyle hs = TEXT_STYLE(FONT_MONO, px, PANGO_WEIGHT_BOLD, px * 0.10,
                              color != NULL ? *color : col_text);
    TextStyle ds = TEXT_STYLE(FONT_MONO, sub_px, PANGO_WEIGHT_NORMAL, 0.0,
                              col_dim);

    text_draw(cr, headline, &hs, G->rx, y, TEXT_LEFT, G->rw);
    text_draw(cr, detail, &ds, G->rx, y + px * 1.7, TEXT_LEFT, G->rw);
}

/* -------------------------------------------------------------- 主绘制 --- */

void details_draw(cairo_t *cr, double width, double height)
{
    Geom       G;
    MetaState  state = metadata_state();
    const TrackMeta *meta = metadata_current();
    const APPconfig *cfg = config_get();
    gint       index = app.track >= 0 ? app.track : 0;
    const AudioTrack *track = config_track_at(cfg, (gsize)index);
    g_autofree gchar *title = NULL;
    g_autofree gchar *artist = NULL;
    const gchar *album = NULL;
    const gchar *date = NULL;
    g_autofree gchar *cover_caption = NULL;
    double     footer_top;

    if (width <= 1.0 || height <= 1.0)
        return;

    G.pad     = clampd(width * 0.055, 26.0, 110.0);
    G.head_px = clampd(height * 0.017, 10.0, 15.0);
    G.head_y  = clampd(height * 0.034, 12.0, 40.0);
    G.gap     = clampd(width * 0.028, 16.0, 48.0);
    G.top     = G.head_y + G.head_px * 2.4;
    G.cover_px = clampd(MIN((width - G.pad * 2.0) * 0.30, height * 0.46),
                        110.0, 460.0);
    G.cover_x = G.pad;
    G.cover_y = G.top;
    G.rx      = G.cover_x + G.cover_px + G.gap;
    G.rw      = MAX(width - G.rx - G.pad, 120.0);
    G.foot_px = clampd(height * 0.016, 9.0, 14.0);
    G.bottom_y = height - G.pad * 0.55;

    footer_top = G.bottom_y - G.foot_px * 1.6;

    /* 背景：压暗整屏，再叠一层封面种子色的柔光 */
    color_set(cr, RGBA(0.043, 0.043, 0.049, 0.93));
    cairo_rectangle(cr, 0.0, 0.0, width, height);
    cairo_fill(cr);

    {
        Rgba  seed = accent(0.0);
        double radius = MAX(width, height) * 0.75;
        cairo_pattern_t *glow = cairo_pattern_create_radial(
            G.pad, G.top * 0.4, 0.0, G.pad, G.top * 0.4, radius);

        cairo_pattern_add_color_stop_rgba(glow, 0.0, seed.r, seed.g, seed.b, 0.10);
        cairo_pattern_add_color_stop_rgba(glow, 1.0, seed.r, seed.g, seed.b, 0.0);
        cairo_set_source(cr, glow);
        cairo_rectangle(cr, 0.0, 0.0, width, height);
        cairo_fill(cr);
        cairo_pattern_destroy(glow);
    }

    /* 数据：READY 用 MB 的字段，其它状态回落到配置里的曲名 / 艺人 */
    if (state == META_STATE_READY && meta != NULL) {
        title  = g_strdup(meta->title);
        artist = g_strdup(meta->artist);
        album  = meta->album;
        date   = meta->release_date;
    } else {
        title  = config_track_title(track);
        artist = g_strdup(track != NULL ? track->artist : NULL);
    }

    /* 封面来源：配置 pic 优先，其次数据库封面 */
    if (track != NULL && track->pic != NULL && *track->pic != '\0')
        cover_caption = g_strdup("CONFIG PIC");
    else if (metadata_cover_path() != NULL)
        cover_caption = g_strdup("COVER ART ARCHIVE");
    else
        cover_caption = g_strdup("NO COVER");

    draw_header(cr, &G, width,
                state == META_STATE_PICK ? "[ ↑ ↓ ]  SELECT    [ ENTER ]  CONFIRM"
                                         : "[ R ]  RESEARCH    [ I / ESC ]  CLOSE");

    /* 封面卡片 */
    {
        double card_pad = clampd(G.cover_px * 0.035, 8.0, 16.0);
        TextStyle cap = TEXT_STYLE(FONT_MONO, G.foot_px, PANGO_WEIGHT_NORMAL,
                                   G.foot_px * 0.16, col_hint);

        card_draw(cr, G.cover_x - card_pad, G.cover_y - card_pad,
                  G.cover_px + card_pad * 2.0, G.cover_px + card_pad * 2.0, 12.0);
        cover_draw(cr, G.cover_x, G.cover_y, G.cover_px);

        text_draw(cr, cover_caption, &cap, G.cover_x,
                  G.cover_y + G.cover_px + card_pad * 2.2, TEXT_LEFT,
                  G.cover_px);
    }

    {
        double y = draw_title(cr, &G, title, artist, album, date, height);

        draw_divider(cr, &G, y);
        y += G.gap * 0.45;

        switch (state) {
        case META_STATE_READY:
            if (meta != NULL)
                draw_fields(cr, &G, meta, y, height);
            else
                draw_status(cr, &G, y, height, "NO DATA", "本地记录不完整。",
                            &col_warn);
            break;

        case META_STATE_PICK:
            draw_candidates(cr, &G, y, height, footer_top);
            break;

        case META_STATE_LOADING:
            draw_status(cr, &G, y, height, "QUERYING MUSICBRAINZ…",
                        "第一次识别一首歌会联网查一次，之后都读本地数据库。",
                        &col_warn);
            break;

        case META_STATE_ERROR:
            draw_status(cr, &G, y, height, "NOT AVAILABLE",
                        or_dash(metadata_error()), &col_error);
            break;

        case META_STATE_IDLE:
        default:
            draw_status(cr, &G, y, height, "NO TRACK",
                        "先选一首歌再打开详情页。", &col_dim);
            break;
        }
    }

    /* 底栏：数据来源 + 状态 */
    {
        TextStyle left = TEXT_STYLE(FONT_MONO, G.foot_px, PANGO_WEIGHT_NORMAL,
                                    G.foot_px * 0.12, col_hint);
        TextStyle right = TEXT_STYLE(FONT_MONO, G.foot_px, PANGO_WEIGHT_NORMAL,
                                     G.foot_px * 0.12, col_hint);
        g_autofree gchar *source = NULL;

        if (state == META_STATE_READY && meta != NULL) {
            source = g_strdup_printf("SOURCE  %s%s%s",
                                     meta->source != NULL ? meta->source
                                                          : "musicbrainz",
                                     meta->cover_path != NULL ? "  ·  COVER CACHED" : "",
                                     "  ·  CACHED LOCALLY");
        } else {
            source = g_strdup(state == META_STATE_PICK
                              ? "SOURCE  LOCAL CANDIDATES · pick one to save"
                              : "SOURCE  musicbrainz.org  ·  local sqlite cache");
        }

        text_draw(cr, source, &left, G.pad, G.bottom_y, TEXT_LEFT,
                  width - G.pad * 2.0);
        text_draw_right(cr, state == META_STATE_READY
                            ? "[ R ]  RE-IDENTIFY    [ I / ESC ]  CLOSE"
                            : "[ ESC ]  CLOSE",
                        &right, width - G.pad, G.bottom_y);
    }
}

/* ---------------------------------------------------------------- 输入 --- */

gboolean details_key(guint keyval, GdkModifierType state)
{
    MetaState meta_state;

    if (!g_open)
        return FALSE;

    if (keymap_match(KEY_DETAILS, keyval, state) ||
        keymap_match(KEY_BACK, keyval, state)) {
        details_close();
        return TRUE;
    }

    if (keymap_match(KEY_QUIT, keyval, state)) {
        if (app.window != NULL)
            gtk_window_close(GTK_WINDOW(app.window));
        return TRUE;
    }

    meta_state = metadata_state();

    if (meta_state == META_STATE_PICK) {
        gint selected = metadata_selected();
        const GPtrArray *cands = metadata_candidates();
        gint n = cands != NULL ? (gint)cands->len : 0;

        switch (keyval) {
        case GDK_KEY_Up:
        case GDK_KEY_k:
            metadata_select(selected - 1);
            return TRUE;
        case GDK_KEY_Down:
        case GDK_KEY_j:
            metadata_select(selected + 1);
            return TRUE;
        case GDK_KEY_Home:
            metadata_select(0);
            return TRUE;
        case GDK_KEY_End:
            metadata_select(n - 1);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            metadata_pick(selected);
            return TRUE;
        default:
            break;
        }
    }

    /* R：认错了就重搜一次；Enter 只在失败 / 无数据时当重试，避免误触清掉记录 */
    if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
        metadata_research();
        return TRUE;
    }

    if ((meta_state == META_STATE_ERROR || meta_state == META_STATE_IDLE) &&
        (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)) {
        metadata_research();
        return TRUE;
    }

    return TRUE;
}
