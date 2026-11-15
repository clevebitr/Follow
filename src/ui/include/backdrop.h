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

#include <cairo.h>
#include <glib.h>

#include "palette.h"

/* 动态背景：模糊封面打底 + 三团从封面里取出的种子色渐变缓慢漂移 + 压暗。
 *
 * CPU 路径（GtkPicture）和 GPU 路径（backdropgl）共用同一份 BackdropFrame：
 * 底图 / 上一张底图、退潮进度、调色板和已经算好的三团光斑。
 * GPU 只需要把底图传成纹理，光斑在 fragment shader 里画。 */

typedef struct {
    float cx, cy, r, alpha;   /* 小纹理上的像素坐标 / 半径 / 透明度 */
    float cr, cg, cb;         /* 已经做过色相偏移的颜色 */
} BackdropBlob;

typedef struct {
    cairo_surface_t *base;        /* 新底图（模糊封面），由 backdrop 持有 */
    cairo_surface_t *prev;        /* 退潮中的旧底图，可能为 NULL */
    int              base_w, base_h;
    gint64           base_gen;    /* 底图版本，GPU 用来判断是否要重新上传 */
    gint64           prev_gen;
    gboolean         has_prev;
    double           tide;        /* 0 = 旧底图还在，1 = 退潮结束 */
    double           phase;       /* 光斑动画相位（秒） */
    gboolean         has_palette;
    Palette          palette;     /* 退潮期间是旧色和新色的插值 */
    BackdropBlob     blob[3];
} BackdropFrame;

/* 换封面时调用：把当前底图留作"退场"的那一张，并开始退潮过渡。
 * 新的底图会在下一帧用新封面重新生成。 */
void backdrop_swap(void);

/* 底图失效：下一帧重建，过渡照常进行 */
void backdrop_invalidate(void);

void backdrop_drop(void);                      /* 退出时全丢掉 */
void backdrop_push(int width, int height);     /* CPU 回退：画一帧并推给 GtkPicture */
gboolean backdrop_tick(gpointer data);         /* CPU 回退的 30fps 定时器回调 */

/* 推进相位 + 粒子模拟；CPU 定时器和 GPU frame clock 都调用它。 */
void backdrop_step(double dt);

/* 准备并返回这一帧的数据。build_base / 退潮清理都在这里完成。 */
const BackdropFrame *backdrop_prepare(int width, int height);

#endif /* FOLLOW_BACKDROP_H */
