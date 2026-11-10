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
