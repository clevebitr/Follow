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

#ifndef FOLLOW_COVER_H
#define FOLLOW_COVER_H

#include <cairo.h>

#include "config.h"

void cover_set(const AudioTrack *track);

void cover_apply_metadata(void);

void cover_clear(void);

cairo_surface_t *cover_base(void);

/* 规范化图床 URL；返回新分配的字符串。 */
gchar *cover_url_normalize(const char *url);

void cover_draw(cairo_t *cr, double x, double y, double size);

#endif
