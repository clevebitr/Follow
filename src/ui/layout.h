#ifndef FOLLOW_LAYOUT_H
#define FOLLOW_LAYOUT_H

#include <cairo.h>
#include <glib.h>

/* 一次绘制需要的全部几何量：所有"怎么摆"的决策都在 layout_compute() 里，
 * stage.c 只管照着坐标画。
 *
 * 版面（照 Sowon2 的截图量的比例，1920×1080）：
 *
 *   note                                    ← 左对齐，在时钟上方
 *   00:00:26                                ← 大时钟（播放位置）
 *   ┌ 当前歌词（可换行，深色衬底）┐
 *
 *                          ┌─────────────┐  ← 封面 + 衬底
 *                          │    cover    │
 *                          │  ────────   │  ← 进度条：封面下方 5px，同宽，不显示总时长
 *                          └─────────────┘
 *                          ♪ 艺人 - 曲名 ♪
 *                              ▛▀▜  QR
 *                             [胶囊]
 *
 *   ┌ 22:41 · LISTENED · TODAY + 热力图 ┐   ← 左下角，深色衬底
 */

#define LYRIC_MAX_LINES 3    /* 歌词最多显示几行，再多就省略 */

typedef struct {
    double pad_x, pad_y;

    /* 左列：备注 / 大时钟 / 歌词 */
    double left_x;
    double left_w;              /* 左列可用宽度（文字限宽） */
    double note_px, note_y;
    double clock_px, clock_y, clock_w, clock_h;
    double lyric_px, lyric_y, lyric_w;
    double hint_px, hint_y;     /* SHUFFLE / REPEAT / LRC 偏移 */

    /* 右列：封面（带衬底）+ 进度条 / 曲名 / 二维码 + 胶囊 */
    double col_cx, col_w;
    double card_pad, card_radius;
    double cover_px, cover_y, cover_card_y, cover_card_h;
    double bar_x, bar_y, bar_w, bar_h;
    double title_px, title_y, title_w;
    double qr_px, qr_y, cell_w, cell_gap;
    double badge_h, badge_y, badge_w;

    /* 左下角统计 */
    double stats_y, stats_clock_px, stats_px;
    double heat_cell, heat_gap, heat_h;
    double stats_card_pad;

    int n_qr;
} Layout;

/* 按窗口尺寸和"有什么内容"算出整屏几何。
 *   clock_text 用来量时钟宽度（太长就把字号缩小，保证不和右列打架）
 *   n_qr 0 / 1 / 2：没有二维码、单个、并排两个 */
void layout_compute(Layout *L, cairo_t *cr, double w, double h,
                    const char *clock_text, int n_qr);

#endif /* FOLLOW_LAYOUT_H */
