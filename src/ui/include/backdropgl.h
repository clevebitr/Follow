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

#ifndef FOLLOW_BACKDROPG_H
#define FOLLOW_BACKDROPG_H

#include <gtk/gtk.h>

/* 动态背景的 GPU 层：GtkGLArea + 全屏 shader。
 *
 * 模糊封面 / 上一张底图是 CPU 生成一次的小纹理（512 宽），
 * 三团漂移光斑、退潮遮罩、音频粒子和压暗全部在 GPU 上合成。
 *
 * 以下情况 backdropgl_ready() 会一直是 FALSE，调用方继续用 GtkPicture + cairo：
 *   - GDK 创建不了 OpenGL 3.3 上下文
 *   - shader 编译 / 链接失败
 *   - FOLLOW_BACKDROPGL=0 显式关闭 */

GtkWidget *backdropgl_new(void);
gboolean   backdropgl_ready(void);
void       backdropgl_clear(void);

#endif /* FOLLOW_BACKDROPG_H */
