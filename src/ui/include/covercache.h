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

#ifndef FOLLOW_COVERCACHE_H
#define FOLLOW_COVERCACHE_H

#include <cairo.h>
#include <glib.h>

#include "palette.h"

/* 播放列表页要同时显示好几张封面，而 cover.c 只养当前播放的这一张。
 * 这里按曲目下标懒加载：本地图直接读，网络图先查磁盘缓存，没有再后台下载。
 * 只给 UI 线程用；surface 由 cache 持有，调用方不要 destroy。 */

void             covercache_sync(void);                 /* 曲目数变化时重建槽位 */
cairo_surface_t *covercache_surface(gsize index);       /* 没加载完返回 NULL，同时会启动加载 */
const Palette   *covercache_palette(gsize index);       /* 还没取到颜色返回 NULL */
double           covercache_reveal(gsize index);        /* 加载后的淡入进度 0..1 */
gboolean         covercache_animating(void);            /* 还有封面在淡入 */
gint64           covercache_version(gsize index);          /* 封面加载时间戳，用作磁贴缓存版本 */
void             covercache_clear(void);                /* 退出时释放 */

#endif /* FOLLOW_COVERCACHE_H */
