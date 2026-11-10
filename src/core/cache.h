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

//TODO:ini define

/* 统一的磁盘缓存目录：<user cache>/follow/
 * 歌词、封面、B 站元数据都放这里，按 key 的 md5 命名。 */

/* 确保目录存在并返回（新分配的字符串） */
gchar *follow_cache_dir(void);

/* 拼一个缓存文件路径：<dir>/<prefix>-<md5(key)>.<ext> */
gchar *follow_cache_file(const gchar *prefix, const gchar *key, const gchar *ext);

#endif /* FOLLOW_CACHE_H */
