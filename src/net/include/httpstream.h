/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_HTTPSTREAM_H
#define FOLLOW_HTTPSTREAM_H

#include <glib.h>

#include <libavformat/avio.h>

/* 以定长 Range 分块读取网络音频，封装为 FFmpeg AVIOContext。 */
AVIOContext *httpstream_avio_open(const char *url, const char *headers, gchar **error);
void         httpstream_avio_free(AVIOContext **pb);

#endif
