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

#ifndef FOLLOW_HTTPSTREAM_H
#define FOLLOW_HTTPSTREAM_H

#include <glib.h>

#include <libavformat/avio.h>

/* 用 libcurl 以「定长 Range」分块读网络音频，包成 FFmpeg 的 AVIOContext。
 * 打开成功返回 AVIOContext，失败返回 NULL 并给出 *error（调用方 g_free）。
 * 释放用 httpstream_avio_free(&pb)：它连带释放缓冲区和内部状态。 */
AVIOContext *httpstream_avio_open(const char *url, const char *headers, gchar **error);
void         httpstream_avio_free(AVIOContext **pb);

#endif /* FOLLOW_HTTPSTREAM_H */
