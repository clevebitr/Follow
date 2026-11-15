/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_APPICON_H
#define FOLLOW_APPICON_H

#include <gtk/gtk.h>

/* 设置窗口 / 任务栏图标。使用透明背景的 logo-mark.svg。 */
void appicon_apply(GtkWidget *window);

/* 释放图标纹理。 */
void appicon_clear(void);

#endif /* FOLLOW_APPICON_H */
