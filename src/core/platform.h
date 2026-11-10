/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_PLATFORM_H
#define FOLLOW_PLATFORM_H

#include <cairo.h>
#include <glib.h>

/* 曲目来源平台：决定胶囊徽标的图标、颜色和名字。
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
void        platform_brand_color(Platform p, double *r, double *g, double *b);

/* 在 (x, y, size, size) 的方块里画品牌色底 + 白色符号 */
void        platform_draw_icon(cairo_t *cr, Platform p, double x, double y, double size);

/* 官方字标有就按 max_h / max_w 等比缩放，返回实际尺寸。
 * 胶囊里直接贴官方 logo —— 没有字标的平台返回 FALSE，
 * 调用方自己退回"图标 + 文字"。 */
gboolean    platform_wordmark_size(Platform p, double max_h, double max_w,
                                   double *out_w, double *out_h);
void        platform_draw_wordmark(cairo_t *cr, Platform p, double x, double y,
                                   double w, double h);

#endif /* FOLLOW_PLATFORM_H */
