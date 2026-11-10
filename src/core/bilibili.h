/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
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
