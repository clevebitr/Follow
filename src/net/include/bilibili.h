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

#ifndef FOLLOW_BILIBILI_H
#define FOLLOW_BILIBILI_H

#include <glib.h>

/* 把 B 站视频页面链接解析成可直接交给解码器的音频直链。
 * 成功返回 0。
 * 失败返回 -1。
 */
int bilibili_resolve(const char *page_url, gchar **audio_url,
                     gchar **title, gchar **artist, gchar **error);

/* 播放 B 站音频流时需要带的请求头（Referer 等），g_free 释放 */
gchar *bilibili_request_headers(void);

#endif /* FOLLOW_BILIBILI_H */
