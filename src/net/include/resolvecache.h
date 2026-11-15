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

#ifndef FOLLOW_RESOLVECACHE_H
#define FOLLOW_RESOLVECACHE_H

#include <glib.h>

#include "source.h"

/* 页面链接 → 解析结果（直链 / headers / 标题 / 艺人 / ranged）。
 * TTL 由 [cache] resolve_ttl 控制；过期后重新请求 API。 */

gboolean resolvecache_load(const gchar *uri, MediaSource *out);
void     resolvecache_save(const gchar *uri, const MediaSource *src);
void     resolvecache_invalidate(const gchar *uri);
void     resolvecache_prune(void);

#endif /* FOLLOW_RESOLVECACHE_H */
