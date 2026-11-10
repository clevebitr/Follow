/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#ifndef FOLLOW_COVER_H
#define FOLLOW_COVER_H

#include <cairo.h>

#include "config.h"

/* 封面 */

/* 换成这张曲目的封面 */
void cover_set(const AudioTrack *track);

/* 丢掉当前封面和缩放缓存 */
void cover_clear(void);

/* 原图（长边 <= 768），动态背景要用；没有封面返回 NULL */
cairo_surface_t *cover_base(void);

/* 在 (x, y, size, size) 的方框里居中画封面；没有封面就画一个占位框 */
void cover_draw(cairo_t *cr, double x, double y, double size);

#endif /* FOLLOW_COVER_H */
