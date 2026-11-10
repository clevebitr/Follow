/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#ifndef FOLLOW_LAYOUT_H
#define FOLLOW_LAYOUT_H

#include <cairo.h>

#define LYRIC_MAX_LINES 3

typedef struct {
    double pad_x, pad_y;

    /* 左列：备注 / 大时钟 / 歌词 */
    double left_x;
    double left_w;
    double note_px, note_y;
    double clock_px, clock_y, clock_w, clock_h;
    double lyric_px, lyric_y, lyric_w;
    double hint_px, hint_y;     /* SHUFFLE / REPEAT / LRC 偏移 */

    /* 右列：封面 + 进度条 / 曲名 / 二维码 + 胶囊 */
    double col_cx, col_w;
    double card_pad, card_radius;
    double cover_px, cover_y, cover_card_y, cover_card_h;
    double bar_x, bar_y, bar_w, bar_h;
    double title_px, title_y, title_w;
    double qr_px, qr_y, cell_w, cell_gap;
    double badge_h, badge_y, badge_w;

    /* 左下角统计*/
    double stats_x, stats_y, stats_w, stats_h;          /* 卡片矩形 */
    double stats_content_x;                             /* 内容左边缘 */
    double stats_content_w;                             /* 内容可用宽度（超了就用省略号） */
    double stats_wall_y, stats_line_y, stats_heat_y;    /* 墙钟 / 时长行 / 热力图上沿 */
    double stats_clock_px, stats_px;
    double heat_cell, heat_gap, heat_h;
    double stats_card_pad, stats_gap_clock, stats_gap_line;

    int n_qr;
} Layout;

/* 算出整屏几何。
 *   clock_text 用来量时钟宽度
 *   n_qr 0 / 1 / 2：没有二维码、单个、并排两个
 *   stats_wall / stats_line 是统计卡片里的两行文字，用真实字号量行高；
 *   NULL 表示按空内容量 */
void layout_compute(Layout *L, cairo_t *cr, double w, double h,
                    const char *clock_text, int n_qr,
                    const char *stats_wall, const char *stats_line);

#endif /* FOLLOW_LAYOUT_H */
