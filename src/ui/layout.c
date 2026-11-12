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

#include "layout.h"
#include "theme.h"

#define CLOCK_PX        0.150   /* 大时钟字号 / h */
#define NOTE_PX         0.026   /* 时钟上方那行备注 / h */
#define LYRIC_PX        0.030   /* 歌词 / h */
#define TITLE_PX        0.030   /* ♪ 艺人 - 曲名 ♪ / h */
#define HINT_PX         0.019
#define COVER_PX        0.267   /* 封面边长 / h（288 / 1080） */
#define QR_PX           0.180   /* 二维码边长 / h（194 / 1080） */
#define BADGE_PX        0.050   /* 平台胶囊高度 / h（57 / 1080） */
#define COL_CENTER      0.62    /* 右列中心 / w（1190 / 1920） */

#define GAP_NOTE_CLOCK  0.008
#define GAP_CLOCK_LYRIC 0.024
#define GAP_LYRIC_NEXT  0.014   /* 当前行 -> 下一行 / h */
#define GAP_LYRIC_HINT  0.012   /* 歌词区下沿 -> 提示行 / h */
#define GAP_COVER_BAR   0.005   /* 进度条离封面底部 5px（1080p） */
#define GAP_CARD_TITLE  0.045
#define GAP_TITLE_QR    0.022
#define GAP_QR_BADGE    0.045

#define LINE_HEIGHT     1.45    /* 一行等宽文字占的高度 / 字号 */

#define STATS_GAP_CLOCK 0.008   /* 墙钟 -> 时长行 / h */
#define STATS_GAP_LINE  0.010   /* 时长行 -> 热力图 / h */
#define STATS_CARD_GROW 1.22    /* 卡片至少比内容大 22% */
#define STATS_SCALE_MIN 0.45    /* 窗口太矮 / 太窄时最多缩到这个比例 */
#define STATS_TEXT_SLACK 8.0    /* Pango 宽度是像素取整，留点余量免得误触省略号 */

typedef struct {
    double w, h;                /* 内容宽 / 高 */
    double wall_h, line_h;      /* 两行文字的真实行高 */
} StatsBox;

static TextStyle measure_style(double px)
{
    return TEXT_STYLE(FONT_HAND, px, PANGO_WEIGHT_NORMAL, 0.0, col_text);
}

double lyric_slot_h(double px)
{
    return px * LYRIC_ROW_EM * (double)LYRIC_SLOT_ROWS;
}

static void stats_styles(const Layout *L, TextStyle *wall, TextStyle *line)
{
    *wall = TEXT_STYLE(FONT_HAND, L->stats_clock_px, PANGO_WEIGHT_NORMAL, 0.0, WHITE(0.78));
    *line = TEXT_STYLE(FONT_MONO, L->stats_px, PANGO_WEIGHT_NORMAL,
                       L->stats_px * 0.14, col_dim);
}

static StatsBox stats_measure(cairo_t *cr, const Layout *L,
                             const char *wall_text, const char *line_text)
{
    TextStyle wall_style, line_style;
    StatsBox  box;
    double    heat_w = 12.0 * (L->heat_cell + L->heat_gap) - L->heat_gap;

    stats_styles(L, &wall_style, &line_style);

    box.wall_h = text_height(cr, wall_text, &wall_style);
    box.line_h = text_height(cr, line_text, &line_style);
    box.w      = MAX(heat_w, MAX(text_width(cr, wall_text, &wall_style),
                                 text_width(cr, line_text, &line_style))
                            + STATS_TEXT_SLACK);
    box.h      = box.wall_h + L->stats_gap_clock
               + box.line_h + L->stats_gap_line + L->heat_h;

    return box;
}

static void clock_place(Layout *L, cairo_t *cr, const char *clock_text, double h)
{
    TextStyle style = measure_style(L->clock_px);
    double    ink;

    L->clock_w = text_width(cr, clock_text, &style);

    /* 时钟过宽时缩字号，避免压到右列 */
    if (L->clock_w > L->left_w && L->clock_w > 0.0) {
        L->clock_px *= L->left_w / L->clock_w;
        style        = measure_style(L->clock_px);
        L->clock_w   = text_width(cr, clock_text, &style);
    }

    L->clock_h = text_height(cr, clock_text, &style);

    /* 手写体上沿留白较大，向上提一点以贴合上面的备注 */
    L->clock_y = L->note_y + L->note_px * LINE_HEIGHT + h * GAP_NOTE_CLOCK
               - L->clock_px * 0.10;

    /* 歌词区上沿取时钟的墨迹底边：手写体排版框底部留白很大，
     * 按框高排会多占一百多像素 */
    ink = text_ink_bottom(cr, clock_text, &style);
    if (ink <= 0.0)
        ink = L->clock_h;

    L->lyric_y = L->clock_y + ink + h * GAP_CLOCK_LYRIC;
}

/* 测量统一使用固定参考串而非实时内容：时钟每秒变化、统计行每小时变化，
 * 手写体又是比例字体，用实时文字测得的卡片尺寸 / 歌词区下沿 / 歌词宽度会
 * 随之逐帧微调。参考串与真实内容字数一致，测量结果恒定。 */
#define CLOCK_REF        "88:88:88"   /* 比例字体里 8 最宽：真实内容不会超参考串 */
#define STATS_WALL_REF   "88:88:88"
#define STATS_LINE_REF   "LISTENED 88h 88m  ·  TODAY 88m"

void layout_compute(Layout *L, cairo_t *cr, double w, double h,
                    const char *clock_text, int n_qr,
                    const char *stats_wall, const char *stats_line)
{
    double    cover_gap, title_gap, qr_gap, need, budget;

    /* 以下排版只使用参考串，绘制仍使用真实文字 */
    clock_text = CLOCK_REF;
    stats_wall = STATS_WALL_REF;
    stats_line = STATS_LINE_REF;

    memset(L, 0, sizeof(*L));

    L->pad_x = clampd(w * 0.046, 14.0, 92.0);
    L->pad_y = clampd(h * 0.060, 14.0, 80.0);
    L->n_qr  = n_qr;

    /* 衬底 */
    L->card_pad    = clampd(h * 0.010, 5.0, 14.0);
    L->card_radius = clampd(h * 0.013, 6.0, 18.0);

    /* 字号 */
    L->note_px        = clampd(h * NOTE_PX, 11.0, 34.0);
    L->clock_px       = clampd(h * CLOCK_PX, 24.0, 300.0);
    L->lyric_px       = clampd(h * LYRIC_PX, 11.0, 44.0);
    /* 预显示的下一行和当前行同字号（Apple Music 那种：只靠明暗 / 模糊区分远近） */
    L->lyric_next_px  = L->lyric_px;
    L->lyric_gap      = MAX(4.0, h * GAP_LYRIC_NEXT);
    L->hint_px        = clampd(h * HINT_PX, 8.0, 20.0);
    L->title_px       = clampd(h * TITLE_PX, 10.0, 44.0);
    L->stats_clock_px = clampd(h * 0.046, 12.0, 48.0);
    L->stats_px       = clampd(h * 0.019, 8.0, 18.0);
    L->heat_cell      = clampd(h * 0.0095, 3.0, 10.0);
    L->heat_gap       = MAX(1.0, L->heat_cell * 0.30);
    L->heat_h         = 7.0 * (L->heat_cell + L->heat_gap) - L->heat_gap;

    /* 左下角统计 */
    L->stats_card_pad  = L->card_pad;
    L->stats_gap_clock = h * STATS_GAP_CLOCK;
    L->stats_gap_line  = h * STATS_GAP_LINE;

    {
        const double avail_w = MAX(w - L->pad_x * 2.0, 1.0);
        const double avail_h = MAX(h - L->pad_y * 2.0, 1.0);
        const double base_clock = L->stats_clock_px;
        const double base_px    = L->stats_px;
        const double base_cell  = L->heat_cell;
        const double base_gap   = L->heat_gap;
        StatsBox     box;
        double       content_w, content_h, pad_h, pad_v, s = 1.0;

        for (int pass = 0; pass < 4; pass++) {
            double need_w, need_h, fit;

            box = stats_measure(cr, L, stats_wall, stats_line);
            need_w = box.w + L->stats_card_pad * 2.0;
            need_h = box.h + L->stats_card_pad * 2.0;

            if (need_w <= avail_w && need_h <= avail_h)
                break;

            fit = MIN(avail_w / need_w, avail_h / need_h);
            if (s * fit < STATS_SCALE_MIN)
                fit = STATS_SCALE_MIN / s;
            s *= fit;

            L->stats_clock_px = MAX(9.0, base_clock * s);
            L->stats_px       = MAX(7.0, base_px * s);
            L->heat_cell      = MAX(2.0, base_cell * s);
            L->heat_gap       = MAX(1.0, base_gap * s);
            L->heat_h         = 7.0 * (L->heat_cell + L->heat_gap) - L->heat_gap;
        }

        box       = stats_measure(cr, L, stats_wall, stats_line);
        content_w = box.w;
        content_h = box.h;

        L->stats_w = MIN(MAX(content_w * STATS_CARD_GROW, content_w + L->stats_card_pad * 2.0),
                         avail_w);
        L->stats_h = MIN(MAX(content_h * STATS_CARD_GROW, content_h + L->stats_card_pad * 2.0),
                         avail_h);

        pad_h = MAX((L->stats_w - content_w) * 0.5, 0.0);
        pad_v = MAX((L->stats_h - content_h) * 0.5, 0.0);

        L->stats_x         = L->pad_x;
        L->stats_y         = h - L->pad_y - L->stats_h;
        L->stats_content_x = L->stats_x + pad_h;
        L->stats_content_w = MAX(L->stats_w - pad_h * 2.0, 1.0);
        L->stats_wall_y    = L->stats_y + pad_v;
        L->stats_line_y    = L->stats_wall_y + box.wall_h + L->stats_gap_clock;
        L->stats_heat_y    = L->stats_line_y + box.line_h + L->stats_gap_line;
    }

    L->hint_y = L->stats_y - L->hint_px * 2.2;

    /* 右列 */
    cover_gap = h * GAP_COVER_BAR;
    title_gap = h * GAP_TITLE_QR;
    qr_gap    = h * GAP_QR_BADGE;

    L->cover_px = h * COVER_PX;
    L->qr_px    = n_qr > 0 ? h * QR_PX : 0.0;
    L->badge_h  = n_qr > 0 ? h * BADGE_PX : 0.0;
    L->bar_h    = MAX(2.0, h * 0.0025);

    /* 窗口太矮 / 太窄：整块等比缩小*/
    need   = L->card_pad * 2.0 + L->cover_px + cover_gap + L->bar_h
           + h * GAP_CARD_TITLE + L->title_px * LINE_HEIGHT + title_gap
           + L->qr_px + (n_qr > 0 ? qr_gap + L->badge_h : 0.0);
    budget = h - 2.0 * L->pad_y;

    /* 窄窗口两栏会撞在一起：这时右列只能占到统计卡片上方 */
    if (w * COL_CENTER - L->cover_px * 0.5 < L->pad_x * 2.0 + L->stats_w)
        budget = MIN(budget, L->stats_y - L->pad_y * 2.0);

    if (need > budget && need > 0.0) {
        double k = MAX(0.25, budget / need);

        L->card_pad *= k;
        L->cover_px *= k;
        L->title_px *= k;
        L->qr_px    *= k;
        L->badge_h  *= k;
        cover_gap   *= k;
        title_gap   *= k;
        qr_gap      *= k;
    }

    L->cell_gap = L->qr_px * 0.16;

    if (n_qr > 1) {
        double row     = (double)n_qr * L->qr_px + (double)(n_qr - 1) * L->cell_gap;
        double max_row = w * 0.50;

        if (row > max_row) {          /* 窄窗口 */
            double k = max_row / row;

            L->qr_px    *= k;
            L->cell_gap *= k;
        }
    }

    L->qr_px  = floor(L->qr_px);      /* 取整 */
    L->cell_w = L->qr_px;
    L->col_w  = MAX(L->cover_px + L->card_pad * 2.0,
                    (double)n_qr * L->qr_px + (double)MAX(0, n_qr - 1) * L->cell_gap);

    L->badge_w = n_qr > 0 ? MIN(L->cell_w * 1.35, L->col_w / (double)n_qr) : 0.0;

    L->col_cx = clampd(w * COL_CENTER,
                       L->pad_x + L->col_w * 0.5,
                       w - L->pad_x - L->col_w * 0.5);

    /* 右列 */
    L->cover_card_y = L->pad_y;
    L->cover_y      = L->cover_card_y + L->card_pad;
    L->bar_x        = L->col_cx - L->cover_px * 0.5;
    L->bar_y        = L->cover_y + L->cover_px + cover_gap;
    L->bar_w        = L->cover_px;
    L->cover_card_h = L->card_pad * 2.0 + L->cover_px + cover_gap + L->bar_h;

    L->title_y = L->cover_card_y + L->cover_card_h + h * GAP_CARD_TITLE;
    L->title_w = MIN(w * 0.42, L->col_w * 1.75);
    L->qr_y    = L->title_y + L->title_px * LINE_HEIGHT + title_gap;
    L->badge_y = L->qr_y + L->qr_px + qr_gap;

    /* 左列：备注 / 大时钟 / 歌词 */
    L->left_x = L->pad_x;

    /* 时钟最多占到右列左边，留一点呼吸空隙 */
    L->left_w = MAX(140.0, (L->col_cx - L->col_w * 0.5) - L->pad_x * 1.6);

    L->note_y = L->pad_y;

    /* 时钟与歌词：先按当前时钟字号试排歌词区，容不下四个槽则逐步缩小时钟
     * （下限 LYRIC_CLOCK_MIN），仍不足时由 lyricgl 收窄间距。 */
    {
        const double orig_px = L->clock_px;
        const double slot    = lyric_slot_h(L->lyric_px);
        const double want    = (double)LYRIC_SLOT_N * slot
                             + (double)(LYRIC_SLOT_N - 1) * L->lyric_gap;
        const double limit   = L->hint_y - h * GAP_LYRIC_HINT;

        clock_place(L, cr, clock_text, h);

        for (int pass = 0; pass < 6; pass++) {
            double avail = limit - L->lyric_y;
            double shrink;

            if (want <= avail || L->clock_px <= orig_px * LYRIC_CLOCK_MIN + 0.01)
                break;

            shrink = CLAMP(1.0 - (want - avail) / L->clock_h, 0.97, 1.0);
            L->clock_px = MAX(orig_px * LYRIC_CLOCK_MIN, L->clock_px * shrink);

            clock_place(L, cr, clock_text, h);
        }
    }

    /* 歌词可视区域：时钟墨迹下方至提示行上方 */
    L->lyric_slot_h = lyric_slot_h(L->lyric_px);
    L->lyric_bottom = MAX(L->lyric_y + L->lyric_px * 1.45,
                          L->hint_y - h * GAP_LYRIC_HINT);
    L->lyric_area_h = L->lyric_bottom - L->lyric_y;

    /* 歌词卡片的宽度上限 = 时钟宽度（居中在时钟下面） */
    L->lyric_w = MIN(L->left_w, L->clock_w);
}
