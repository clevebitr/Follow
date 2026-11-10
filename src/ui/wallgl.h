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

#ifndef FOLLOW_WALLGL_H
#define FOLLOW_WALLGL_H

#include <gtk/gtk.h>

/* 歌曲墙的 GPU 层：GtkGLArea + 一个 textured quad shader。
 * cairo 只负责把每首歌 / 每种尺寸的磁贴预渲染成纹理，墙的相机平移、
 * 选中弹出、压暗和淡入都交给 GPU 合成。
 *
 * 如果 GL 上下文创建失败，wallgl_ready() 会一直是 FALSE，
 * playlist 会自动退回原来的整屏 cairo 绘制。 */

typedef struct {
    gsize    track;
    double   x, y, w, h;     /* 屏幕坐标 */
    gboolean selected;
    gboolean hovered;
    gboolean current;
    float    reveal;         /* 封面淡入进度 0..1 */
} WallGlTile;

typedef struct {
    WallGlTile *tiles;       /* 非选中磁贴（绘制顺序）后跟选中的那一块 */
    int         n_tiles;
    int         selected;    /* tiles 里 selected 磁贴的下标，-1 = 没有 */
    double      view_w, view_h;

    double      dim_top, dim_bottom;
    double      bg_r, bg_g, bg_b;

    /* 封面置色：两团柔光铺满整屏（Folia 的 root 背景做法） */
    gboolean    tint;
    double      tint_seed_r, tint_seed_g, tint_seed_b;
    double      tint_alt_r, tint_alt_g, tint_alt_b;

    double      selected_pop;      /* 选中磁贴的弹出缩放 */
    double      glow_r, glow_g, glow_b;
    gboolean    glow;

    double      vis;               /* 0..1：进场 / 退场 */
    double      offset_y;          /* 进场时整体下移的像素 */
} WallGlFrame;

GtkWidget *wallgl_new(void);
gboolean   wallgl_ready(void);
void       wallgl_set_frame(const WallGlFrame *frame);
void       wallgl_set_active(gboolean active);
void       wallgl_clear(void);

#endif /* FOLLOW_WALLGL_H */
