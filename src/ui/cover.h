#ifndef FOLLOW_COVER_H
#define FOLLOW_COVER_H

#include <cairo.h>

#include "config.h"

/* 封面：本地文件直接读，http(s) 地址在后台线程下载并落到缓存目录。
 * 解码统一走 gdk-pixbuf（通道序可靠），再转成 cairo surface。 */

/* 换成这张曲目的封面（track 为 NULL / 没有 pic 就是清空） */
void cover_set(const AudioTrack *track);

/* 丢掉当前封面和缩放缓存 */
void cover_clear(void);

/* 原图（长边 <= 768），动态背景要用；没有封面返回 NULL */
cairo_surface_t *cover_base(void);

/* 在 (x, y, size, size) 的方框里居中画封面；没有封面就画一个占位框 */
void cover_draw(cairo_t *cr, double x, double y, double size);

#endif /* FOLLOW_COVER_H */
