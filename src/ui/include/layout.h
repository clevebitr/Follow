/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_LAYOUT_H
#define FOLLOW_LAYOUT_H

#include <cairo.h>

typedef struct {
    double pad_x, pad_y;

    double left_x;
    double left_w;
    double note_px, note_y;
    double clock_px, clock_y, clock_w, clock_h;
    double lyric_px, lyric_y, lyric_w;
    double lyric_bottom;
    double lyric_next_px;
    double lyric_area_h;
    double lyric_gap;
    double lyric_slot_h;
    double hint_px, hint_y;

    double col_x, col_cx, col_w;
    double card_pad, card_radius;
    double cover_px, cover_y, cover_card_y, cover_card_h;
    double bar_x, bar_y, bar_w, bar_h;
    double title_px, title_x, title_y, title_w;
    double next_px;
    double qr_px, qr_y, cell_w, cell_gap;
    double badge_h, badge_y, badge_w;

    double next_x, next_y, next_w, next_h;

    double stats_x, stats_y, stats_w, stats_h;
    double stats_heat_x, stats_heat_y;
    double stats_heat_dy;
    double stats_ink_bot;
    double stats_bottom;
    double stats_text_x;
    double stats_wall_y, stats_line_y;
    double stats_clock_px, stats_px;
    double heat_cell, heat_gap, heat_h;
    double stats_gap_clock, stats_gap_heat;

    int n_qr;
} Layout;

/* 歌词四槽：预推出列 / 当前句 / 预演一 / 预演二；槽高固定。 */
#define LYRIC_SLOT_N    4
#define LYRIC_SLOT_ROWS 2
#define LYRIC_ROW_EM    1.30
#define LYRIC_CLOCK_MIN 0.85
#define LYRIC_W_GROW    1.20
#define LYRIC_W_MIN     0.46
#define TITLE_LINES     2

double lyric_slot_h(double px);

/* 计算整屏几何；clock_text 与 stats_* 用于测量。 */
void layout_compute(Layout *L, cairo_t *cr, double w, double h,
                    const char *clock_text, int n_qr,
                    const char *stats_wall, const char *stats_line);

#endif
