/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#ifndef FOLLOW_BACKDROP_H
#define FOLLOW_BACKDROP_H

#include <glib.h>

/* 动态背景：模糊封面打底 + 三团从封面里取出的种子色渐变缓慢漂移 + 压暗。 */

/* 换封面时调用：把当前底图留作"退场"的那一张，并开始退潮过渡。
 * 新的底图会在下一帧用新封面重新生成。 */
void backdrop_swap(void);

/* 底图失效：下一帧重建，过渡照常进行 */
void backdrop_invalidate(void);

void backdrop_drop(void);                      /* 退出时全丢掉 */
void backdrop_push(int width, int height);     /* 画一帧并推给 GtkPicture */
gboolean backdrop_tick(gpointer data);         /* 30fps 定时器回调 */

#endif /* FOLLOW_BACKDROP_H */
