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

#include "stage.h"

#include <string.h>

#include "config.h"
#include "cover.h"
#include "covercache.h"
#include "help.h"
#include "layout.h"
#include "media.h"
#include "lyricgl.h"
#include "player.h"
#include "playlist.h"
#include "qrcard.h"
#include "stats.h"
#include "theme.h"
#include "uidbg.h"
#include "ui.h"
#include "wallgl.h"

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

/* 播放错误 > 歌词请求中 > 歌词错误 > 加载中：这些占嵌在歌词位置上，
 * 用普通字重、淡色，和真正的歌词区分开 */
static const gchar *status_line(Rgba *color)
{
    const gchar *error = media_error(&app.media);

    if (error != NULL) {
        *color = col_error;
        return error;
    }

    if (app.source_error != NULL && *app.source_error != '\0') {
        *color = col_error;
        return app.source_error;
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

/* 热力图 + 墙钟 + 收听时长：无卡片衬底，位置全部由 layout_compute() 给出。
 * 热力图在左，高度与右侧两行文字的总高一致。 */
static void draw_stats(cairo_t *cr, const Layout *L, const gchar *wall, const gchar *line)
{
    TextStyle wall_style = TEXT_STYLE(FONT_CLOCK, L->stats_clock_px, PANGO_WEIGHT_NORMAL,
                                     0.0, WHITE(0.78));
    TextStyle line_style = TEXT_STYLE(FONT_MONO, L->stats_px, PANGO_WEIGHT_NORMAL,
                                     L->stats_px * 0.14, col_dim);
    double    text_w = MAX(L->stats_w - (L->stats_text_x - L->stats_x), 1.0);

    draw_heatmap(cr, L, L->stats_heat_x, L->stats_heat_y);

    /* 限宽 = 文字列宽：极端窄窗口下用省略号收住 */
    text_draw(cr, wall, &wall_style, L->stats_text_x, L->stats_wall_y,
              TEXT_LEFT, text_w);
    text_draw(cr, line, &line_style, L->stats_text_x, L->stats_line_y,
              TEXT_LEFT, text_w);
}

/* 歌词。
 *
 * GPU 路径（默认）：把可视区域交给 lyricgl，排版 / 自动折行 / 弹簧 / 扫描头
 *   都在那一层里做（左对齐、按行绘制、没有辉光和模糊）。
 *
 * cairo 回退（没有 GL）：只画当前行 + 下一行，同样左对齐、自动折行、不发光。 */

/* 当前句高亮色：取封面种子色并降低鲜艳度。保留色相，亮度上限 0.80、
 * 饱和度乘 0.55，再向白色混合 10%，与周围白字连贯且仍可区分。 */
#define ACCENT_VALUE   0.80   /* 亮度上限 */
#define ACCENT_FLOOR   0.66   /* 太暗的封面抬到这个亮度 */
#define ACCENT_SATUR   0.55   /* 饱和度系数 */
#define ACCENT_WHITE   0.10   /* 往白字混的比例 */

static Rgba seed_accent(void)
{
    const Palette *p = &app.palette;
    double         r = p->has ? p->seed_r : 0.62;
    double         g = p->has ? p->seed_g : 0.74;
    double         b = p->has ? p->seed_b : 0.98;
    double         mx = MAX(r, MAX(g, b));
    double         avg;

    /* 亮度：过暗的抬高（保持色相），过亮的压低 */
    if (mx > 0.001 && mx < ACCENT_FLOOR) {
        double k = ACCENT_FLOOR / mx;

        r *= k;
        g *= k;
        b *= k;
    } else if (mx > ACCENT_VALUE) {
        double k = ACCENT_VALUE / mx;

        r *= k;
        g *= k;
        b *= k;
    }

    /* 饱和度：向同亮度灰靠拢，色相不变 */
    avg = (r + g + b) / 3.0;
    r   = avg + (r - avg) * ACCENT_SATUR;
    g   = avg + (g - avg) * ACCENT_SATUR;
    b   = avg + (b - avg) * ACCENT_SATUR;

    /* 向白色轻微混合，使与白字的过渡更自然 */
    r += (1.0 - r) * ACCENT_WHITE;
    g += (1.0 - g) * ACCENT_WHITE;
    b += (1.0 - b) * ACCENT_WHITE;

    /* 封面本身为灰时补一点冷色底，避免高亮与白字难以区分 */
    if (MAX(r, MAX(g, b)) - MIN(r, MIN(g, b)) < 0.06) {
        r += 0.02;
        g += 0.05;
        b += 0.10;
    }

    return RGBA(clampd(r, 0.0, 1.0), clampd(g, 0.0, 1.0),
                clampd(b, 0.0, 1.0), 1.0);
}

static gboolean draw_lyrics(cairo_t *cr, const Layout *L)
{
    uidbg_begin_layer(UIDBG_LAYER_LYRIC);

    /* 这一层每帧重画：先清掉上一帧的记录（GPU 路径下随后由 lyricgl
     * 的 render 重建，cairo 路径下由下面的绘制重建） */
    uidbg_layer_clear(UIDBG_LAYER_LYRIC);

    if (lyricgl_ready()) {
        LyricViewGeom geom;

        geom.x      = L->left_x;
        geom.w      = L->lyric_w;
        geom.top    = L->lyric_y;
        geom.height = L->lyric_area_h;
        geom.px     = L->lyric_px;
        geom.gap    = L->lyric_px * 0.55;   /* 行距（不含行内折行的行高） */

        /* 扫描头要的是"减掉歌词偏移之后"的播放位置 */
        geom.position_ms = (media_position(&app.media) * 1000.0)
                         - (double)app.lrc_offset_ms;
        geom.playing     = media_is_playing(&app.media);
        geom.accent      = seed_accent();

        /* 歌词在 GPU 层画，布局 / 弹簧 / 扫描头都在 lyricgl 里；
         * 它自己会把真实几何报给 uidbg（layer = lyric） */
        lyricgl_set_view(app.lyrics, app.lyric_index, &geom);

        uidbg_begin_layer(UIDBG_LAYER_STAGE);
        return TRUE;
    }

    /* ---------- cairo 回退 ---------- */
    {
        const double t    = fx_t(&app.fx_lyric);
        const double rise = L->lyric_px * 1.1;
        TextStyle    cur  = TEXT_STYLE(FONT_MONO, L->lyric_px, PANGO_WEIGHT_BOLD,
                                       L->lyric_px * 0.10, WHITE(0.96));
        TextStyle    nxt  = TEXT_STYLE(FONT_MONO, L->lyric_next_px, PANGO_WEIGHT_BOLD,
                                       L->lyric_next_px * 0.10, WHITE(0.45));
        gboolean     has_cur  = app.lyric_line != NULL && *app.lyric_line != '\0';
        gboolean     has_next = app.lyric_next != NULL && *app.lyric_next != '\0';
        gboolean     drew     = FALSE;
        double       y        = L->lyric_y + rise * (1.0 - t);

        /* 只在歌词区域里画：不会渗到下面的提示行 / 统计卡片里 */
        cairo_save(cr);
        cairo_rectangle(cr, L->left_x, L->lyric_y, L->lyric_w, L->lyric_area_h);
        cairo_clip(cr);

        if (app.lyric_prev != NULL && *app.lyric_prev != '\0' && t < 1.0) {
            TextStyle out = cur;

            out.color.a = 0.96 * (1.0 - t);
            text_draw_block(cr, app.lyric_prev, &out, L->left_x, y - rise * t,
                            TEXT_LEFT, L->lyric_w, 0);
            drew = TRUE;
        }

        if (has_cur) {
            if (t < 1.0)
                cur.color.a *= t;

            y += text_draw_block(cr, app.lyric_line, &cur, L->left_x, y,
                                 TEXT_LEFT, L->lyric_w, 0);
            drew = TRUE;
        }

        if (has_next) {
            TextStyle ahead = nxt;

            ahead.color.a *= t;

            if (ahead.color.a > 0.02) {
                if (has_cur)
                    y += L->lyric_gap;

                text_draw_block(cr, app.lyric_next, &ahead, L->left_x, y,
                                TEXT_LEFT, L->lyric_w, 0);
            }

            drew = TRUE;
        }

        cairo_restore(cr);

        uidbg_begin_layer(UIDBG_LAYER_STAGE);
        return drew;
    }
}

static void draw_left(cairo_t *cr, const Layout *L, const gchar *clock_text)
{
    const gchar *note = current_note();
    TextStyle    style;

    /* 备注 */
    if (note != NULL) {
        style = TEXT_STYLE(FONT_MONO, L->note_px, PANGO_WEIGHT_NORMAL,
                           L->note_px * 0.16, WHITE(0.50));
        text_draw(cr, note, &style, L->left_x, L->note_y, TEXT_LEFT, L->left_w);
    }

    /* 大时钟 */
    style = TEXT_STYLE(FONT_CLOCK, L->clock_px, PANGO_WEIGHT_NORMAL, 0.0, col_text);
    text_draw(cr, clock_text, &style, L->left_x, L->clock_y, TEXT_LEFT, 0.0);

    /* 歌词 / 状态：有错误或加载中就先报状态，否则画歌词 */
    {
        Rgba         color = col_dim;
        const gchar *line  = status_line(&color);

        if (line != NULL) {
            style = TEXT_STYLE(FONT_MONO, L->lyric_px, PANGO_WEIGHT_NORMAL,
                               L->lyric_px * 0.10, color);
            /* 和歌词同一套左对齐锚点；放不下就省略号（只占一行） */
            text_draw_block(cr, line, &style, L->left_x, L->lyric_y,
                            TEXT_LEFT, L->lyric_w, 1);
            if (lyricgl_ready())
                lyricgl_clear();
        } else {
            draw_lyrics(cr, L);
        }
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
    text_draw(cr, mode->str, &style, L->left_x, L->hint_y, TEXT_LEFT, L->left_w);

    g_string_free(mode, TRUE);
}

/* ---------- NEXT 预告卡片 ---------- */

#define NEXT_ANIM_SEC 0.30   /* 切卡动画时长 */

static gint   g_next_shown = -2;   /* 上一次绘制时显示的曲目 */
static gint   g_next_from  = -1;   /* 切换时被推回牌堆的曲目 */
static gint64 g_next_t0;           /* 切换起始时间（0 = 静止） */

/* 卡片的封面缩略图：取不到时触发加载，随后由动画泵重绘 */
static cairo_surface_t *next_thumb(gint idx, double *alpha)
{
    cairo_surface_t *img;

    covercache_sync();

    img = idx >= 0 ? covercache_surface((gsize)idx) : NULL;

    *alpha = img != NULL ? 0.35 + 0.65 * covercache_reveal((gsize)idx) : 1.0;


    return img;
}

/* 画一张卡片：底卡 + 缩略图 + 曲名 / 艺人。dy / scale 用于推进动画 */
static void next_card_paint(cairo_t *cr, const Layout *L, gint idx,
                            double alpha, double dy, double scale)
{
    const APPconfig  *cfg = config_get();
    const AudioTrack *track = NULL;
    double            pad   = L->next_h * 0.13;
    double            thumb = L->next_h - pad * 2.0;
    double            cx    = L->next_x + L->next_w * 0.5;
    double            cy    = L->next_y + L->next_h * 0.5;
    double            img_a = 1.0;
    cairo_surface_t  *img;

    if (alpha <= 0.004)
        return;

    if (idx >= 0 && idx < (gint)cfg->n_tracks)
        track = config_track_at(cfg, (gsize)idx);

    cairo_save(cr);
    cairo_translate(cr, cx, cy + dy);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, -cx, -cy);

    cairo_push_group(cr);

    card_draw(cr, L->next_x, L->next_y, L->next_w, L->next_h, L->card_radius * 0.8);

    /* "NEXT" 作为卡片背景水印：右对齐、低对比，压在内容下面 */
    {
        TextStyle tag = TEXT_STYLE(FONT_MONO, L->next_px * 1.25, PANGO_WEIGHT_BOLD,
                                  L->next_px * 0.30, WHITE(0.11));
        double    tag_w = text_width(cr, "NEXT", &tag);
        double    tag_y = L->next_y + (L->next_h - L->next_px * 1.25 * 1.45) * 0.5;

        cairo_save(cr);
        path_round_rect(cr, L->next_x, L->next_y, L->next_w, L->next_h,
                        L->card_radius * 0.8);
        cairo_clip(cr);
        text_draw(cr, "NEXT", &tag, L->next_x + L->next_w - pad - tag_w, tag_y,
                  TEXT_LEFT, 0.0);
        cairo_restore(cr);
    }

    img = next_thumb(idx, &img_a);

    if (img != NULL) {
        double iw = (double)cairo_image_surface_get_width(img);
        double ih = (double)cairo_image_surface_get_height(img);
        double s  = thumb / MAX(MIN(iw, ih), 1.0);
        double ox = (thumb - iw * s) * 0.5;
        double oy = (thumb - ih * s) * 0.5;

        cairo_save(cr);
        path_round_rect(cr, L->next_x + pad, L->next_y + pad,
                        thumb, thumb, thumb * 0.16);
        cairo_clip(cr);
        cairo_translate(cr, L->next_x + pad + ox, L->next_y + pad + oy);
        cairo_scale(cr, s, s);
        cairo_set_source_surface(cr, img, 0.0, 0.0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint_with_alpha(cr, img_a);
        cairo_restore(cr);
    } else {
        color_set(cr, WHITE(0.07));
        path_round_rect(cr, L->next_x + pad, L->next_y + pad,
                        thumb, thumb, thumb * 0.16);
        cairo_fill(cr);
    }

    {
        double  text_x = L->next_x + pad + thumb + pad * 0.9;
        double  text_w = (L->next_x + L->next_w - pad) - text_x;
        double  line1  = L->next_px * 1.45;
        double  top    = L->next_y + (L->next_h - line1 * 2.0) * 0.5;
        TextStyle name = TEXT_STYLE(FONT_MONO, L->next_px, PANGO_WEIGHT_BOLD,
                                    L->next_px * 0.04, WHITE(0.92));
        TextStyle artist = TEXT_STYLE(FONT_MONO, L->next_px * 0.78,
                                      PANGO_WEIGHT_NORMAL, L->next_px * 0.10, col_dim);

        if (track != NULL) {
            text_draw(cr, track->title != NULL ? track->title : "—", &name,
                      text_x, top, TEXT_LEFT, text_w);
            text_draw(cr, track->artist != NULL ? track->artist : "", &artist,
                      text_x, top + line1, TEXT_LEFT, text_w);
        } else {
            text_draw(cr, "—", &name, text_x, top, TEXT_LEFT, text_w);
        }
    }

    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

/* 牌堆：两张错位的空卡片 */
static void next_deck_paint(cairo_t *cr, const Layout *L, double lift)
{
    double pad = L->next_h * 0.13;

    for (int i = 2; i >= 1; i--) {
        double inset = pad * 0.5 * (double)i;
        double rise  = pad * (0.55 + 0.35 * lift) * (double)i;

        color_set(cr, WHITE(0.05 * (double)(3 - i)));
        path_round_rect(cr, L->next_x + inset, L->next_y - rise,
                        L->next_w - inset * 2.0, L->next_h,
                        L->card_radius * 0.8);
        cairo_fill(cr);
    }
}

/* NEXT 预告：小标题 + 叠放卡片。切歌时旧卡被推回牌堆、新卡自前方落位。 */
static void draw_next_card(cairo_t *cr, const Layout *L)
{
    gint64 now = g_get_monotonic_time();
    gint   idx = player_queue_preview();
    double k;

    if (idx != g_next_shown) {
        if (g_next_shown >= -1) {
            g_next_from = g_next_shown;
            g_next_t0   = now;
            ui_animate();
        }

        g_next_shown = idx;
    }

    k = g_next_t0 != 0
      ? clampd((double)(now - g_next_t0) / (NEXT_ANIM_SEC * (double)G_USEC_PER_SEC), 0.0, 1.0)
      : 1.0;

    if (k >= 1.0)
        g_next_t0 = 0;

    next_deck_paint(cr, L, k < 1.0 ? k : 1.0);

    if (k < 1.0) {
        /* 旧卡：向后收、上移、淡出 */
        next_card_paint(cr, L, g_next_from, 1.0 - k, -L->next_h * 0.10 * k,
                        1.0 - 0.06 * k);
    }

    /* 新卡：从后方放大落位 */
    {
        double e = 1.0 - pow(1.0 - k, 3.0);

        next_card_paint(cr, L, idx, 0.45 + 0.55 * e,
                        L->next_h * 0.12 * (1.0 - e), 0.96 + 0.04 * e);
    }
}

gboolean stage_next_animating(void)
{
    return g_next_t0 != 0;
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
        double            px    = L->title_px;
        double            need;
        TextStyle         style = TEXT_STYLE(FONT_MONO, px, PANGO_WEIGHT_NORMAL,
                                            px * 0.05, col_text);

        /* 曲名放不进封面列时先缩字号（下限 0.7），尽量保持单行、左右居中 */
        need = text_width(cr, title, &style);

        if (need > L->title_w && need > 0.0) {
            px    = px * MAX(0.7, L->title_w / need);
            style = (TextStyle)TEXT_STYLE(FONT_MONO, px, PANGO_WEIGHT_NORMAL,
                                          px * 0.05, col_text);
        }

        text_draw_block(cr, title, &style, L->title_x, L->title_y,
                        TEXT_CENTER, L->title_w, TITLE_LINES);
    }

    draw_next_card(cr, L);

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

    uidbg_begin_layer(UIDBG_LAYER_STAGE);

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

    /* 快照只是给退场动画用的离屏副本：别让它把这一帧的元素再记一遍 */
    uidbg_suppress(TRUE);
    draw_stage(cr, w, h);
    uidbg_suppress(FALSE);

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

    uidbg_frame_begin();

    /* 播放列表是全屏页：盖掉 stage，只留下面那层动态背景 */
    if (playlist_is_open()) {
        /* 退出歌曲墙时把主界面垫在下面，页面淡出时能看见它 */
        if (playlist_is_closing())
            draw_stage(cr, width, height);

        /* GL 层可用时墙在 GPU 上画，并由 wallgl 的 frame clock 重建帧；
         * 这张 cairo 画布保持透明（墙打开时会被隐藏）。
         * 没有 GL 时退回整屏 cairo。 */
        if (!wallgl_ready()) {
            playlist_draw(cr, (double)width, (double)height);

            /* 这一帧刚好播完退场：立刻把主界面补上，不留黑帧 */
            if (!playlist_is_open())
                draw_stage(cr, width, height);
        }

        uidbg_frame_end();
        return;
    }

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

    uidbg_frame_end();
}

/* 墙的表头 / 底栏 / 帮助页：画在 GL 层上面的透明画布上，不参与磁贴绘制。 */
void stage_chrome_render(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data)
{
    (void)area;
    (void)data;

    if (width <= 0 || height <= 0 || !playlist_is_open())
        return;

    uidbg_begin_layer(UIDBG_LAYER_WALL_CHROME);
    uidbg_layer_clear(UIDBG_LAYER_WALL_CHROME);

    playlist_draw_chrome(cr, (double)width, (double)height);

    if (app.help) {
        uidbg_layer_clear(UIDBG_LAYER_WALL_CHROME);
        help_draw(cr, (double)width, (double)height);
    }
}
