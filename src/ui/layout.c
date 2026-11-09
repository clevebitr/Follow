#include <math.h>
#include <string.h>

#include "layout.h"
#include "theme.h"

/* 全部比例都是从 Sowon2 的 1920×1080 截图里量出来的。
 * 改动这里就等于调整整屏观感，所以集中放在一起。 */
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
#define GAP_COVER_BAR   0.005   /* 进度条离封面底部 5px（1080p） */
#define GAP_CARD_TITLE  0.045
#define GAP_TITLE_QR    0.022
#define GAP_QR_BADGE    0.045

#define LINE_HEIGHT     1.45    /* 一行等宽文字占的高度 / 字号 */

static TextStyle measure_style(double px)
{
    return TEXT_STYLE(FONT_HAND, px, PANGO_WEIGHT_NORMAL, 0.0, col_text);
}

void layout_compute(Layout *L, cairo_t *cr, double w, double h,
                    const char *clock_text, int n_qr)
{
    TextStyle clock_style;
    double    cover_gap, title_gap, qr_gap, need, budget;
    double    stats_block_h, stats_w;

    memset(L, 0, sizeof(*L));

    L->pad_x = clampd(w * 0.046, 14.0, 92.0);
    L->pad_y = clampd(h * 0.060, 14.0, 80.0);
    L->n_qr  = n_qr;

    /* 衬底（封面 / 歌词 / 胶囊 / 统计共用同一套观感） */
    L->card_pad    = clampd(h * 0.010, 5.0, 14.0);
    L->card_radius = clampd(h * 0.013, 6.0, 18.0);

    /* ---------- 字号：只跟窗口高有关 ---------- */
    L->note_px        = clampd(h * NOTE_PX, 11.0, 34.0);
    L->clock_px       = clampd(h * CLOCK_PX, 24.0, 300.0);
    L->lyric_px       = clampd(h * LYRIC_PX, 11.0, 44.0);
    L->hint_px        = clampd(h * HINT_PX, 8.0, 20.0);
    L->title_px       = clampd(h * TITLE_PX, 10.0, 44.0);
    L->stats_clock_px = clampd(h * 0.046, 12.0, 48.0);
    L->stats_px       = clampd(h * 0.019, 8.0, 18.0);
    L->heat_cell      = clampd(h * 0.0095, 3.0, 10.0);
    L->heat_gap       = MAX(1.0, L->heat_cell * 0.30);
    L->heat_h         = 7.0 * (L->heat_cell + L->heat_gap) - L->heat_gap;

    /* ---------- 左下角统计：先定位，右列要靠它判断能不能用满整高 ---------- */
    L->stats_card_pad = L->card_pad;
    stats_block_h = L->stats_clock_px * 1.30 + h * 0.008
                  + L->stats_px * 1.60 + h * 0.010
                  + L->heat_h;
    stats_w = MAX(12.0 * (L->heat_cell + L->heat_gap) - L->heat_gap, h * 0.33)
            + L->stats_card_pad * 2.0;

    L->stats_y = h - L->pad_y - stats_block_h - L->stats_card_pad * 2.0;
    L->hint_y  = L->stats_y - L->hint_px * 2.2;   /* 状态提示贴在统计卡片上沿 */

    /* ---------- 右列：先定各项尺寸 ---------- */
    cover_gap = h * GAP_COVER_BAR;
    title_gap = h * GAP_TITLE_QR;
    qr_gap    = h * GAP_QR_BADGE;

    L->cover_px = h * COVER_PX;
    L->qr_px    = n_qr > 0 ? h * QR_PX : 0.0;
    L->badge_h  = n_qr > 0 ? h * BADGE_PX : 0.0;
    L->bar_h    = MAX(2.0, h * 0.0025);

    /* 窗口太矮 / 太窄：整块等比缩小，封面不会被挤成负数 */
    need   = L->card_pad * 2.0 + L->cover_px + cover_gap + L->bar_h
           + h * GAP_CARD_TITLE + L->title_px * LINE_HEIGHT + title_gap
           + L->qr_px + (n_qr > 0 ? qr_gap + L->badge_h : 0.0);
    budget = h - 2.0 * L->pad_y;

    /* 窄窗口两栏会撞在一起：这时右列只能占到统计卡片上方 */
    if (w * COL_CENTER - L->cover_px * 0.5 < L->pad_x * 2.0 + stats_w)
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

    /* 两个二维码并排时整行会比封面宽，所以列宽取"封面"和"二维码行"中较大的那个，
     * 二维码本身保持截图里的大小。 */
    L->cell_gap = L->qr_px * 0.16;

    if (n_qr > 1) {
        double row     = (double)n_qr * L->qr_px + (double)(n_qr - 1) * L->cell_gap;
        double max_row = w * 0.50;

        if (row > max_row) {          /* 窄窗口：整行等比缩小 */
            double k = max_row / row;

            L->qr_px    *= k;
            L->cell_gap *= k;
        }
    }

    L->qr_px  = floor(L->qr_px);      /* 取整：两个二维码尺寸绝对一致 */
    L->cell_w = L->qr_px;
    L->col_w  = MAX(L->cover_px + L->card_pad * 2.0,
                    (double)n_qr * L->qr_px + (double)MAX(0, n_qr - 1) * L->cell_gap);

    /* 胶囊可以比二维码宽一点，但两个并排时不能互相撞上 */
    L->badge_w = n_qr > 0 ? MIN(L->cell_w * 1.35, L->col_w / (double)n_qr) : 0.0;

    /* 截图里封面右边缘正好落在 62% + 半个列宽处；窄窗口自动收回内侧 */
    L->col_cx = clampd(w * COL_CENTER,
                       L->pad_x + L->col_w * 0.5,
                       w - L->pad_x - L->col_w * 0.5);

    /* ---------- 右列：自上而下堆 ---------- */
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

    /* ---------- 左列：备注 / 大时钟 / 歌词 ---------- */
    L->left_x = L->pad_x;

    /* 时钟最多占到右列左边，留一点呼吸空隙 */
    L->left_w = MAX(140.0, (L->col_cx - L->col_w * 0.5) - L->pad_x * 1.6);

    L->note_y = L->pad_y;

    /* 时钟太长就缩字号，保证永远压不到右列 */
    clock_style = measure_style(L->clock_px);
    L->clock_w  = text_width(cr, clock_text, &clock_style);

    if (L->clock_w > L->left_w && L->clock_w > 0.0) {
        L->clock_px *= L->left_w / L->clock_w;
        clock_style  = measure_style(L->clock_px);
        L->clock_w   = text_width(cr, clock_text, &clock_style);
    }

    L->clock_h = text_height(cr, clock_text, &clock_style);

    /* 手写体的上沿留白很大，往上提一点，视觉上才贴着上面的备注 */
    L->clock_y = L->note_y + L->note_px * LINE_HEIGHT + h * GAP_NOTE_CLOCK
               - L->clock_px * 0.10;

    L->lyric_y = L->clock_y + L->clock_h + h * GAP_CLOCK_LYRIC;

    /* 歌词卡片的宽度上限 = 时钟宽度（居中在时钟下面） */
    L->lyric_w = MIN(L->left_w, L->clock_w);
}
