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

#ifndef FOLLOW_BACKDROPG_H
#define FOLLOW_BACKDROPG_H

#include <gtk/gtk.h>

/* 动态背景 GPU 层：光斑、退潮、粒子与压暗在 shader 中合成。 */

GtkWidget *backdropgl_new(void);
gboolean   backdropgl_ready(void);
void       backdropgl_clear(void);

#endif
