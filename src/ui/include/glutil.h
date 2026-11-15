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

#ifndef FOLLOW_GLUTIL_H
#define FOLLOW_GLUTIL_H

#include <cairo.h>
#include <epoxy/gl.h>
#include <gtk/gtk.h>

/* GL 工具：shader 编译、纹理上传、单位四边形、HiDPI 视口。 */

GLuint gl_util_program(const char *vs_src, const char *fs_src, const char *tag);

void   gl_util_quad(GLuint *vao, GLuint *vbo);

/* cairo ARGB32 → GL 纹理；失败返回 0。 */
GLuint gl_util_texture_from_surface(cairo_surface_t *surface);

int    gl_util_scale(GtkWidget *widget);

int    gl_util_viewport(GtkWidget *widget);

const char *gl_util_renderer(void);

/* 软件光栅化检测，仅用于日志。 */
gboolean   gl_util_software(void);

void       gl_util_log_context(const char *tag);

#endif
