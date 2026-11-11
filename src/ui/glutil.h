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

#ifndef FOLLOW_GLUTIL_H
#define FOLLOW_GLUTIL_H

#include <cairo.h>
#include <epoxy/gl.h>
#include <gtk/gtk.h>

/* 歌曲墙和动态背景共用的最小 GL 工具：编译 shader、上传 cairo surface、
 * 建一块单位四边形、读窗口的 HiDPI 缩放系数。
 *
 * 所有 GtkGLArea 的 render 回调里都要用 gl_util_viewport() 设置视口：
 * GTK 给的是逻辑像素，OpenGL 的 framebuffer 是设备像素，2K / 4K 上
 * 不乘缩放系数会把画面画到左下角。 */

GLuint gl_util_program(const char *vs_src, const char *fs_src, const char *tag);

/* 单位四边形：location 0 = xy，location 1 = uv。 */
void   gl_util_quad(GLuint *vao, GLuint *vbo);

/* cairo ARGB32（小端 = BGRA 预乘）→ GL 纹理。失败返回 0。 */
GLuint gl_util_texture_from_surface(cairo_surface_t *surface);

/* 窗口缩放系数（>= 1）。 */
int    gl_util_scale(GtkWidget *widget);

/* 把视口设成设备像素；返回缩放系数。 */
int    gl_util_viewport(GtkWidget *widget);

const char *gl_util_renderer(void);

/* GL_RENDERER 看起来是软件光栅化（llvmpipe / SwiftShader / GDI Generic ...）。
 * 只用来打日志，不强制回退：有些机器上软件 GL 仍然比整屏 cairo 快。 */
gboolean   gl_util_software(void);

void       gl_util_log_context(const char *tag);

#endif /* FOLLOW_GLUTIL_H */
