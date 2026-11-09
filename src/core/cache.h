#ifndef FOLLOW_CACHE_H
#define FOLLOW_CACHE_H

#include <glib.h>

/* 统一的磁盘缓存目录：<user cache>/follow/
 * 歌词、封面、B 站元数据都放这里，按 key 的 md5 命名。 */

/* 确保目录存在并返回（新分配的字符串） */
gchar *follow_cache_dir(void);

/* 拼一个缓存文件路径：<dir>/<prefix>-<md5(key)>.<ext> */
gchar *follow_cache_file(const gchar *prefix, const gchar *key, const gchar *ext);

#endif /* FOLLOW_CACHE_H */
