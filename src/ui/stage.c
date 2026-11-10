#include "stage.h"

#include <string.h>

#include "config.h"
#include "cover.h"
#include "help.h"
#include "layout.h"
#include "player.h"
#include "qrcard.h"
#include "stats.h"
#include "theme.h"
#include "ui.h"

/* 时间统一 "00:00:26"：位数固定，宽度不会随秒数跳 */
static gchar *format_time(double seconds)
{
    int total = (int)(MAX(seconds, 0.0) + 0.5);

    return g_strdup_printf("%02d:%02d:%02d",
                           total / 3600, (total / 60) % 60, total % 60);
}

/* "3h 20m" / "12m" */
static void format_span(guint seconds, gchar *buf, gsize len)
{
    guint hours = seconds / 3600;
    guint mins  = (seconds % 3600) / 60;

    if (hours > 0)
        g_snprintf(buf, len, "%uh %02um", hours, mins);
    else
        g_snprintf(buf, len, "%um", mins);
}

/* 播放错误 > 歌词请求中 > 歌词错误 > 加载中 > 当前歌词行 */
static const gchar *subtitle_line(Rgba *color)
{
    const gchar *error = media_error(&app.media);

    if (error != NULL) {
        *color = col_error;
        return error;
    }

    if (app.lyrics_pending) {
        *color = WHITE(0.40);
        return "LYRICS...";
    }

    if (app.lyric_error != NULL) {
        *color = col_warn;
        return app.lyric_error;
    }

    if (media_is_loading(&app.media)) {
        *color = WHITE(0.45);
        return "LOADING";
    }

    if (app.lyric_line != NULL && *app.lyric_line != '\0') {
        *color = WHITE(0.68);
        return app.lyric_line;
    }

    return NULL;
}

static const gchar *current_note(void)
{
    const APPconfig  *cfg   = config_get();
    const AudioTrack *track = config_track_at(cfg, app.track >= 0 ? (gsize)app.track : 0);

    return config_track_note(cfg, track);
}


static void draw_heatmap(cairo_t *cr, const Layout *L, double x, double y)
{
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    const int     today_wd = g_date_time_get_day_of_week(now) - 1;   /* 0 = 周一 */
    const guint   max_sec  = stats_max_in_range(&app.stats, 84);
    const Rgba    seed     = app.palette.has
        ? RGBA(app.palette.seed_r, app.palette.seed_g, app.palette.seed_b, 1.0)
        : RGBA(0.60, 0.70, 0.95, 1.0);
    const double  step = L->heat_cell + L->heat_gap;

    /* 最近 12 周，列 11 是本周*/
    for (int col = 0; col < 12; col++) {
        for (int row = 0; row < 7; row++) {
            int    days_ago = today_wd + (11 - col) * 7 - row;
            double level    = 0.0;

            if (days_ago < 0)
                continue;

            if (max_sec > 0) {
                guint seconds = stats_seconds_days_ago(&app.stats, days_ago);

                if (seconds > 0)
                    level = 0.28 + 0.66 * MIN(1.0, (double)seconds / (double)max_sec);
            }

            if (level <= 0.0)
                color_set(cr, WHITE(0.07));
            else
                color_set(cr, RGBA(seed.r, seed.g, seed.b, level));

            path_round_rect(cr, x + (double)col * step, y + (double)row * step,
                            L->heat_cell, L->heat_cell, L->heat_cell * 0.22);
            cairo_fill(cr);
        }
    }
}

/* "LISTENED 3h 20m  ·  TODAY 12m" */
static gchar *stats_summary(void)
{
    gchar total[32];
    gchar today[32];

    format_span(stats_total_seconds(&app.stats), total, sizeof(total));
    format_span(stats_seconds_today(&app.stats), today, sizeof(today));

    return g_strdup_printf("LISTENED %s  ·  TODAY %s", total, today);
}

/* 墙钟 + 收听时长 + 热力图：位置全部由 layout_compute() 给定 */
static void draw_stats(cairo_t *cr, const Layout *L, const gchar *wall, const gchar *line)
{
    TextStyle wall_style = TEXT_STYLE(FONT_HAND, L->stats_clock_px, PANGO_WEIGHT_NORMAL,
                                     0.0, WHITE(0.78));
    TextStyle line_style = TEXT_STYLE(FONT_MONO, L->stats_px, PANGO_WEIGHT_NORMAL,
                                     L->stats_px * 0.14, col_dim);

    card_draw(cr, L->stats_x, L->stats_y, L->stats_w, L->stats_h, L->card_radius);

    /* 限宽 = 卡片内容宽度：极端窄窗口下至少会用省略号收住，不会冲出卡片 */
    text_draw(cr, wall, &wall_style, L->stats_content_x, L->stats_wall_y,
              TEXT_LEFT, L->stats_content_w);
    text_draw(cr, line, &line_style, L->stats_content_x, L->stats_line_y,
              TEXT_LEFT, L->stats_content_w);

    draw_heatmap(cr, L, L->stats_content_x, L->stats_heat_y);
}

static void draw_left(cairo_t *cr, const Layout *L, const gchar *clock_text)
{
    const gchar *note = current_note();
    TextStyle    style;

    /* 备注：时钟上方，左对齐 */
    if (note != NULL) {
        style = TEXT_STYLE(FONT_MONO, L->note_px, PANGO_WEIGHT_NORMAL,
                           L->note_px * 0.16, WHITE(0.50));
        text_draw(cr, note, &style, L->pad_x, L->note_y, TEXT_LEFT, L->left_w);
    }

    /* 大时钟：整屏的主角 */
    style = TEXT_STYLE(FONT_HAND, L->clock_px, PANGO_WEIGHT_NORMAL, 0.0, col_text);
    text_draw(cr, clock_text, &style, L->pad_x, L->clock_y, TEXT_LEFT, 0.0);

    /* 歌词：深色衬底 + 自动换行，居中在时钟下面 */
    {
        Rgba         color = col_dim;
        const gchar *line  = subtitle_line(&color);
        double       pad_x, pad_y, natural, text_w, card_w, card_h, card_x, text_h;

        if (line == NULL)
            return;

        style = TEXT_STYLE(FONT_MONO, L->lyric_px, PANGO_WEIGHT_NORMAL,
                           L->lyric_px * 0.10, color);

        pad_x   = L->lyric_px * 0.85;
        pad_y   = L->lyric_px * 0.45;
        natural = text_width(cr, line, &style);

        /* 留几像素余量：Pango 的宽度是像素取整的，卡得太紧会在最后一行折一个字 */
        text_w  = MIN(L->lyric_w, natural + 8.0);

        text_h = text_block_height(cr, line, &style, text_w, LYRIC_MAX_LINES);

        /* 衬底 ≈ 文字宽度的 1.5 倍（长句顶着时钟宽度时自然贴住） */
        card_w = MAX(MIN(L->lyric_w, natural) + pad_x * 2.0,
                     MIN(L->lyric_w, natural) * 1.5);
        card_w = MIN(card_w, L->lyric_w + pad_x * 2.0);
        card_h = text_h + pad_y * 2.0;
        card_x = MAX(L->pad_x, L->pad_x + (L->clock_w - card_w) * 0.5);

        card_draw(cr, card_x, L->lyric_y, card_w, card_h, L->card_radius);
        text_draw_block(cr, line, &style, card_x + card_w * 0.5, L->lyric_y + pad_y,
                        TEXT_CENTER, text_w, LYRIC_MAX_LINES);
    }
}

/* 播放模式 / 歌词偏移：只在非默认状态出现，贴在统计卡片上沿 */
static void draw_hint(cairo_t *cr, const Layout *L)
{
    GString *mode;
    TextStyle style;

    if (app.repeat == REPEAT_ALL && !app.shuffle && app.lrc_offset_ms == 0)
        return;

    mode = g_string_new(NULL);

    if (app.shuffle)
        g_string_append(mode, "SHUFFLE");

    if (app.repeat != REPEAT_ALL) {
        if (mode->len > 0)
            g_string_append(mode, "  ·  ");
        g_string_append(mode, app.repeat == REPEAT_ONE ? "REPEAT ONE" : "NO REPEAT");
    }

    if (app.lrc_offset_ms != 0)
        g_string_append_printf(mode, "%sLRC %+dms",
                               mode->len > 0 ? "  ·  " : "", app.lrc_offset_ms);

    style = TEXT_STYLE(FONT_MONO, L->hint_px, PANGO_WEIGHT_NORMAL,
                       L->hint_px * 0.14, col_hint);
    text_draw(cr, mode->str, &style, L->pad_x, L->hint_y, TEXT_LEFT, L->left_w);

    g_string_free(mode, TRUE);
}

/* ---------- 右列：封面 + 进度条 / ♪ 曲名 ♪ / 二维码 ---------- */

static void draw_right(cairo_t *cr, const Layout *L)
{
    const double position = media_position(&app.media);
    const double duration = media_duration(&app.media);
    g_autofree gchar *label = player_label();
    double card_x = L->col_cx - (L->cover_px + L->card_pad * 2.0) * 0.5;

    /* 封面和进度条装在同一个衬底里 */
    card_draw(cr, card_x, L->cover_card_y, L->cover_px + L->card_pad * 2.0,
              L->cover_card_h, L->card_radius);

    cover_draw(cr, L->bar_x, L->cover_y, L->cover_px);

    /* 进度条：封面下方 5px、和封面同宽；不显示总时长 */
    color_set(cr, WHITE(0.12));
    path_round_rect(cr, L->bar_x, L->bar_y, L->bar_w, L->bar_h, L->bar_h * 0.5);
    cairo_fill(cr);

    if (duration > 0.0) {
        double played = L->bar_w * clampd(position / duration, 0.0, 1.0);

        if (played > 0.5) {
            color_set(cr, WHITE(0.82));
            path_round_rect(cr, L->bar_x, L->bar_y, played, L->bar_h, L->bar_h * 0.5);
            cairo_fill(cr);
        }
    }

    if (label != NULL) {
        g_autofree gchar *title = g_strdup_printf("♪ %s ♪", label);
        TextStyle         style = TEXT_STYLE(FONT_MONO, L->title_px, PANGO_WEIGHT_NORMAL,
                                            L->title_px * 0.05, col_text);

        text_draw(cr, title, &style, L->col_cx, L->title_y, TEXT_CENTER, L->title_w);
    }

    qrcard_draw(cr, L);
}

/* ---------- 主绘制 ---------- */

/* 画一帧"现在的画面"。背景不在这里画：下面那层 GtkPicture 负责，这层是透明的。 */
static void draw_stage(cairo_t *cr, int width, int height)
{
    g_autoptr(GDateTime) now        = g_date_time_new_now_local();
    g_autofree gchar    *clock_text = format_time(media_position(&app.media));
    g_autofree gchar    *wall       = g_date_time_format(now, "%H:%M:%S");
    g_autofree gchar    *stats      = stats_summary();
    int                  n_qr       = (app.qr[0].text != NULL ? 1 : 0)
                                    + (app.qr[1].text != NULL ? 1 : 0);
    Layout               L;

    /* 布局和绘制用同一份文字：卡片尺寸就是按它量出来的，不会再"内容比卡片高" */
    layout_compute(&L, cr, (double)width, (double)height, clock_text, n_qr, wall, stats);

    draw_left(cr, &L, clock_text);
    draw_right(cr, &L);
    draw_stats(cr, &L, wall, stats);
    draw_hint(cr, &L);
}

void stage_snapshot(void)
{
    int              w, h;
    cairo_surface_t *shot;
    cairo_t         *cr;

    if (app.canvas == NULL || app.track < 0)
        return;

    w = gtk_widget_get_width(app.canvas);
    h = gtk_widget_get_height(app.canvas);

    if (w <= 0 || h <= 0)
        return;

    shot = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(shot) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(shot);
        return;
    }

    cr = cairo_create(shot);
    draw_stage(cr, w, h);
    cairo_destroy(cr);

    g_clear_pointer(&app.snapshot, cairo_surface_destroy);
    app.snapshot = shot;

    /* 比背景慢半拍、也更长一点：背景先退潮，组件随后撤桌重上 */
    fx_begin_later(&app.fx_stage, 0.85, 0.15);
    ui_animate();
}

void stage_render(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    double p;

    (void)area;
    (void)data;

    if (width <= 0 || height <= 0)
        return;

    p = app.snapshot != NULL ? fx_t(&app.fx_stage) : 1.0;

    if (app.snapshot != NULL && p < 1.0) {
        /* 撤桌重上：旧画面整块往左退场（同时淡出），新画面从右边接上来。
         * 和背景退潮同一个方向、同一条缓动曲线，两层叠在一起就是"交融"。 */
        cairo_save(cr);
        cairo_translate(cr, -p * (double)width, 0.0);
        cairo_set_source_surface(cr, app.snapshot, 0.0, 0.0);
        cairo_paint_with_alpha(cr, 1.0 - p);
        cairo_restore(cr);

        cairo_save(cr);
        cairo_translate(cr, (1.0 - p) * (double)width, 0.0);
        draw_stage(cr, width, height);
        cairo_restore(cr);
    } else {
        draw_stage(cr, width, height);

        if (app.snapshot != NULL)          /* 过渡走完，快照可以扔了 */
            g_clear_pointer(&app.snapshot, cairo_surface_destroy);
    }

    if (app.help)
        help_draw(cr, (double)width, (double)height);
}
