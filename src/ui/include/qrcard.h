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

#ifndef FOLLOW_QRcard_H
#define FOLLOW_QRcard_H

#include <cairo.h>

#include "config.h"
#include "layout.h"

/* 二维码与平台胶囊；按像素尺寸预渲染并缓存。 */

void qrcard_update(const AudioTrack *track);
void qrcard_clear(void);
void qrcard_draw(cairo_t *cr, const Layout *L);

#endif
