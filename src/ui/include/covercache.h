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

#ifndef FOLLOW_COVERCACHE_H
#define FOLLOW_COVERCACHE_H

#include <cairo.h>
#include <glib.h>

#include "palette.h"

/* 播放列表封面缓存：按曲目下标懒加载，仅在 UI 线程使用。 */

void             covercache_sync(void);
cairo_surface_t *covercache_surface(gsize index);
const Palette   *covercache_palette(gsize index);
double           covercache_reveal(gsize index);
gboolean         covercache_animating(void);
gint64           covercache_version(gsize index);
void             covercache_clear(void);

#endif
