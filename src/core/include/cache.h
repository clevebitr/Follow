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

#ifndef FOLLOW_CACHE_H
#define FOLLOW_CACHE_H

#include <glib.h>

/* 统一的磁盘缓存目录：默认 <用户缓存目录>/follow，可在 [cache] dir 里改
 * （支持 ~ 和相对路径）。歌词、封面、B 站元数据、收听统计都放这里，
 * 按 key 的 md5 命名。 */

/* 确保目录存在并返回（新分配的字符串） */
gchar *follow_cache_dir(void);

/* 拼一个缓存文件路径：<dir>/<prefix>-<md5(key)>.<ext> */
gchar *follow_cache_file(const gchar *prefix, const gchar *key, const gchar *ext);

/* 统计 / 清理某一类缓存（文件名前缀 prefix-）。按 mtime 从旧到新删到上限以内。 */
guint64 cache_prefix_bytes(const gchar *prefix);
void    cache_prune_prefix(const gchar *prefix, guint64 max_bytes);

#endif /* FOLLOW_CACHE_H */
