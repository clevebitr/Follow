#ifndef FOLLOW_BILIBILI_H
#define FOLLOW_BILIBILI_H

#include <glib.h>

/* 把 B 站视频页面链接解析成可直接交给解码器的音频直链。
 * 支持：
 *   https://www.bilibili.com/video/BV1xx411c7mD
 *   https://b23.tv/xxxxxxx            （短链，先跟重定向）
 *   https://www.bilibili.com/video/av170001
 *
 * 成功返回 0：*audio_url 必非 NULL，*title / *artist 可能为 NULL。
 * 失败返回 -1，*error 是新分配的说明。输出都用 g_free 释放。
 */
int bilibili_resolve(const char *page_url, gchar **audio_url,
                     gchar **title, gchar **artist, gchar **error);

/* 播放 B 站音频流时需要带的请求头（Referer 等），g_free 释放 */
gchar *bilibili_request_headers(void);

#endif /* FOLLOW_BILIBILI_H */
