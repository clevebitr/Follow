/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#ifndef FOLLOW_QRcard_H
#define FOLLOW_QRcard_H

#include <cairo.h>

#include "config.h"
#include "layout.h"

/* 二维码规则：
 *
 *   曲目有网络页面（page= 或网络 link）    → 一个二维码，下面是平台徽标
 *   [qrcode] link 是网络地址且不重复      → 再加一个，下面是地址胶囊
 *   本地曲目 + 没有自定义链接             → 一个都不显示
 *
 * 二维码按"像素边长"预渲染并缓存，所以两个不同版本的码画出来一样大。
 */

void qrcard_update(const AudioTrack *track);   /* 换曲目时调用 */
void qrcard_clear(void);                       /* 退出时释放 */
void qrcard_draw(cairo_t *cr, const Layout *L);

#endif /* FOLLOW_QRcard_H */
