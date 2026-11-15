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
#define NEXT_H          0.072   /* NEXT 卡片高度 / h */
#define NEXT_GAP        0.020   /* NEXT 卡片与其上方内容的间距 / h */
#define GAP_BADGE_NEXT  0.032   /* 胶囊 -> NEXT 卡片的额外间距 / h */
#define SHIFT_X         0.050   /* 两列整体右移 / w */
#define STATS_LIFT      0.035   /* 统计块相对底边内边距再上抬 / h */
#define GAP_TITLE_QR    0.022
#define GAP_QR_BADGE    0.045

#define LINE_HEIGHT     1.45    /* 一行等宽文字占的高度 / 字号 */
#define GUTTER_TEXT     1.5     /* 封面列与文本栏的间距 / pad_x */

#define STATS_GAP_CLOCK 0.008   /* 墙钟 -> 时长行 / h */
#define HEAT_GAP_RATIO  0.30    /* 热力图格子间隙 / 格高 */
#define STATS_GAP_HEAT  0.020   /* 热力图 -> 文字 / h */
#define STATS_TEXT_SLACK 8.0    /* Pango 宽度是像素取整，留点余量免得误触省略号 */

typedef struct {
    double w, h;                /* 内容宽 / 高 */
    double text_w;              /* 右侧两行文字的宽度 */
    double wall_h, line_h;      /* 两行文字的真实行高 */
} StatsBox;

static TextStyle measure_style(double px)
{
    return TEXT_STYLE(FONT_CLOCK, px, PANGO_WEIGHT_NORMAL, 0.0, col_text);
}

double lyric_slot_h(double px)
{
    return px * LYRIC_ROW_EM * (double)LYRIC_SLOT_ROWS;
}

static void stats_styles(const Layout *L, TextStyle *wall, TextStyle *line)
{
    *wall = TEXT_STYLE(FONT_CLOCK, L->stats_clock_px, PANGO_WEIGHT_NORMAL, 0.0, WHITE(0.78));
    *line = TEXT_STYLE(FONT_MONO, L->stats_px, PANGO_WEIGHT_NORMAL,
                       L->stats_px * 0.14, col_dim);
}

/* 统计块：左侧热力图 + 右侧"墙钟 / 收听时长"两行文字。
 * 热力图高度与文字块高度相等（7 行格子 + 6 个间隙解出格高）。 */
static StatsBox stats_measure(cairo_t *cr, Layout *L,
                              const char *wall_text, const char *line_text)
{
    TextStyle wall_style, line_style;
    StatsBox  box;
    double    text_w, text_h;

    stats_styles(L, &wall_style, &line_style);

    box.wall_h = text_height(cr, wall_text, &wall_style);
    box.line_h = text_height(cr, line_text, &line_style);

    text_w = MAX(text_width(cr, wall_text, &wall_style),
                 text_width(cr, line_text, &line_style)) + STATS_TEXT_SLACK;
    text_h = box.wall_h + L->stats_gap_clock + box.line_h;

    /* 热力图：方块格，按**墨迹**与右侧文字对齐 —— 顶边齐墙钟字形上沿、
     * 底边齐收听行字形下沿（排版框自带行距留白，按框对齐会多出一行格子）。 */
    {
        double ink_top = text_ink_top(cr, wall_text, &wall_style);
        double ink_bot = box.wall_h + L->stats_gap_clock
                       + text_ink_bottom(cr, line_text, &line_style);

        if (ink_bot - ink_top < text_h * 0.5) {
            ink_top = 0.0;
            ink_bot = text_h;
        }

        L->stats_heat_dy = ink_top;
        L->stats_ink_bot = ink_bot;
        L->heat_h        = ink_bot - ink_top;
    }

    L->heat_cell = MAX(2.0, L->heat_h / (7.0 + 6.0 * HEAT_GAP_RATIO));
    L->heat_gap  = MAX(1.0, L->heat_cell * HEAT_GAP_RATIO);
    L->heat_h    = 7.0 * L->heat_cell + 6.0 * L->heat_gap;

    box.text_w = text_w;
    box.w      = 12.0 * L->heat_cell + 11.0 * L->heat_gap
               + L->stats_gap_heat + text_w;
    box.h      = L->heat_h;

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
    StatsBox  box = { 0 };

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
    L->next_px        = clampd(h * 0.016, 10.0, 20.0);
    L->next_h         = clampd(h * NEXT_H, 46.0, 88.0);

    /* 统计块（无卡片衬底）：热力图贴在文字左侧，两者等高 */
    L->stats_gap_clock = h * STATS_GAP_CLOCK;
    L->stats_gap_heat  = h * STATS_GAP_HEAT;

    {
        const double avail_w = MAX(w - L->pad_x * 2.0, 1.0);
        const double avail_h = MAX(h - L->pad_y * 2.0, 1.0);

        for (int pass = 0; pass < 4; pass++) {
            double fit;

            box = stats_measure(cr, L, stats_wall, stats_line);

            if (box.w <= avail_w && box.h <= avail_h)
                break;

            fit = MIN(avail_w / box.w, avail_h / box.h);

            L->stats_clock_px = MAX(9.0, L->stats_clock_px * fit);
            L->stats_px       = MAX(7.0, L->stats_px * fit);
        }

        box          = stats_measure(cr, L, stats_wall, stats_line);
        L->stats_w   = box.w;
        L->stats_h   = box.h;
        /* 统计块按视觉底边（墨迹）对齐：热力图底边 = 收听行底部 = 块底边 */
        L->stats_bottom = h - L->pad_y - h * STATS_LIFT;
        L->stats_y      = L->stats_bottom - L->stats_ink_bot;
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
           + h * GAP_CARD_TITLE + L->title_px * LINE_HEIGHT * TITLE_LINES + title_gap
           + NEXT_GAP * h + L->next_px * LINE_HEIGHT + L->next_h
           + L->qr_px + (n_qr > 0 ? qr_gap + L->badge_h : 0.0);
    budget = h - 2.0 * L->pad_y;

    /* 窄窗口两列会挤在一起：此时封面列只能占到统计卡片上方 */
    {
        const double rail_min = MAX(360.0, w * 0.34);

        if (L->pad_x + L->col_w + L->pad_x * 0.6 + rail_min > w - L->pad_x)
            budget = MIN(budget, L->stats_y - L->pad_y * 2.0);
    }

    if (need > budget && need > 0.0) {
        double k = MAX(0.25, budget / need);

        L->card_pad *= k;
        L->cover_px *= k;
        L->title_px *= k;
        L->next_px  *= k;
        L->next_h   *= k;
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

    /* 封面 / 二维码列：整体右移 SHIFT_X（右栏随之移动，取得视觉对称） */
    L->col_x  = L->pad_x + w * SHIFT_X;
    L->col_cx = L->col_x + L->col_w * 0.5;

    /* 右列 */
    L->cover_card_y = L->pad_y;
    L->cover_y      = L->cover_card_y + L->card_pad;
    L->bar_x        = L->col_cx - L->cover_px * 0.5;
    L->bar_y        = L->cover_y + L->cover_px + cover_gap;
    L->bar_w        = L->cover_px;
    L->cover_card_h = L->card_pad * 2.0 + L->cover_px + cover_gap + L->bar_h;

    /* 封面列纵向排布：封面卡贴顶，NEXT 卡片贴底（底边与统计块底边齐平）；
     * 中间的曲名 / 二维码 / 胶囊把剩余空间均分到三处间距，
     * 保证 NEXT 小标题不会挤到胶囊上。 */
    {
        const double stack_h = L->title_px * LINE_HEIGHT * TITLE_LINES
                             + L->qr_px + (n_qr > 0 ? L->badge_h : 0.0);
        const double gap_sum = h * (GAP_CARD_TITLE + GAP_TITLE_QR + GAP_QR_BADGE);
        const double top     = L->cover_card_y + L->cover_card_h;
        /* 卡片上方留 NEXT_GAP + GAP_BADGE_NEXT，避免贴住胶囊 */
        const double bottom  = L->stats_bottom - L->next_h
                             - h * (NEXT_GAP + GAP_BADGE_NEXT);
        double       extra   = ((bottom - top) - (stack_h + gap_sum)) / 3.0;

        /* 空间不足时先把三处间距压到 40%，仍不足则缩小 NEXT 卡片 */
        if (extra < 0.0) {
            extra = MAX(extra, -gap_sum / 3.0 * 0.6);
            L->next_h = MAX(46.0, L->next_h + ((bottom - top) - (stack_h + gap_sum)
                                               - extra * 3.0));
        }

        L->title_y  = top + h * GAP_CARD_TITLE + extra;
        title_gap   = h * GAP_TITLE_QR + extra;
        qr_gap      = h * GAP_QR_BADGE + extra;

        L->qr_y     = L->title_y + L->title_px * LINE_HEIGHT * TITLE_LINES + title_gap;
        L->badge_y  = L->qr_y + L->qr_px + qr_gap;

        L->next_y       = L->stats_bottom - L->next_h;
        L->next_w       = L->col_w;
        L->next_x       = L->col_cx - L->next_w * 0.5;
    }

    /* 文本栏：备注 / 大时钟 / 歌词，位于封面列右侧并延伸至右内边距。
     * 与封面列之间留出明显间距（GUTTER_TEXT / 窗口比例）。 */
    {
        double gutter = MAX(L->pad_x * GUTTER_TEXT, w * 0.05);

        L->left_x = (L->col_x + L->col_w) + gutter;
    }
    L->left_w = MAX(140.0, (w - L->pad_x) - L->left_x);

    /* 曲名与封面同轴居中：宽度取封面列宽（= 两码总宽），超出则换行 */
    L->title_w = L->col_w;
    L->title_x = L->col_cx;

    /* 统计块：与文本栏左对齐，热力图在左、文字在右，两者等高 */
    L->stats_x      = L->left_x;
    L->stats_w      = box.w;
    L->stats_heat_x = L->stats_x;
    L->stats_heat_y = L->stats_y + L->stats_heat_dy;
    L->stats_text_x = L->stats_x + (box.w - box.text_w);
    L->stats_wall_y = L->stats_y;
    L->stats_line_y = L->stats_y + box.wall_h + L->stats_gap_clock;



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

    /* 歌词列宽：至少为时钟宽度的 LYRIC_W_GROW 倍，并保证可读行长
     * （窗口宽度的 LYRIC_W_MIN），上限为文本栏可用宽度。 */
    L->lyric_w = MIN(L->left_w, MAX(L->clock_w * LYRIC_W_GROW, w * LYRIC_W_MIN));

}
