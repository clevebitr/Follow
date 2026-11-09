#ifndef FOLLOW_PLATFORM_H
#define FOLLOW_PLATFORM_H

#include <cairo.h>
#include <glib.h>

/* 曲目来源平台：决定二维码下面那个胶囊徽标的图标、颜色和名字。
 * 平台从 URL 的主机名自动识别，也可以在 ini 里用 platform= 强制指定。 */

typedef enum {
    PLATFORM_UNKNOWN = 0,
    PLATFORM_BILIBILI,
    PLATFORM_APPLE_MUSIC,
    PLATFORM_YOUTUBE,
    PLATFORM_SPOTIFY,
    PLATFORM_SOUNDCLOUD,
    PLATFORM_NETEASE,
    PLATFORM_BANDCAMP
} Platform;

Platform    platform_from_url(const char *url);
Platform    platform_from_name(const char *name);

const char *platform_name(Platform p);        /* 显示用，全大写 */
const char *platform_id(Platform p);          /* 配置里用的短名 */
void        platform_brand_color(Platform p, double *r, double *g, double *b);

/* 在 (x, y, size, size) 的方块里画品牌色底 + 白色符号 */
void        platform_draw_icon(cairo_t *cr, Platform p, double x, double y, double size);

#endif /* FOLLOW_PLATFORM_H */
