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

#ifndef FOLLOW_COVER_H
#define FOLLOW_COVER_H

#include <cairo.h>

#include "config.h"

/* 封面 */

/* 换成这张曲目的封面。
 * 优先级：配置里的 pic > 元数据数据库封面（[metadata] cover = true）。
 * 元数据稍后才回来时，主循环会再调 cover_apply_metadata()。 */
void cover_set(const AudioTrack *track);

/* 当前曲目没有配置 pic 时，把数据库里已经缓存的封面装上。
 * 已经有封面 / 关掉 [metadata] cover 时是空操作。 */
void cover_apply_metadata(void);

/* 丢掉当前封面和缩放缓存 */
void cover_clear(void);

/* 原图（长边 <= 768），动态背景要用；没有封面返回 NULL */
cairo_surface_t *cover_base(void);

/* 把图床 URL 规整成可下载的方形 JPEG（B 站 @... 后缀 / .avif）。
 * 播放列表的封面缓存也用这个规则，返回新分配的字符串。 */
gchar *cover_url_normalize(const char *url);

/* 在 (x, y, size, size) 的方框里居中画封面；没有封面就画一个占位框 */
void cover_draw(cairo_t *cr, double x, double y, double size);

#endif /* FOLLOW_COVER_H */
