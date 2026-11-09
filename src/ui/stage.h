#ifndef FOLLOW_STAGE_H
#define FOLLOW_STAGE_H

#include <gtk/gtk.h>

/* 主画面（即时模式）：GTK 只提供窗口 / 键盘 / 音频，整屏由 stage_render()
 * 用 cairo 画出来 —— 和 raylib 那样一帧一画，只是没有逐帧动画，
 * 所以只在「秒数 / 歌词行 / 状态 / 尺寸」变化时重画。
 *
 * 下面那层动态背景是另一个 GtkPicture，这层画布是透明的。 */
void stage_render(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data);

/* 切歌前调用：把当前画面拍成快照，"撤桌重上"时用来退场。
 * 只有在已经有画面（canvas 有尺寸）时才有意义。 */
void stage_snapshot(void);

#endif /* FOLLOW_STAGE_H */
