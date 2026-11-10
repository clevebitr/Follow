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

#ifndef FOLLOW_YOUTUBE_H
#define FOLLOW_YOUTUBE_H

#include <glib.h>

gchar *youtube_video_id(const gchar *url);

int youtube_resolve(const char *page_url, gchar **audio_url,
                    gchar **title, gchar **artist, gchar **error);

gchar *youtube_request_headers(void);

#endif /* FOLLOW_YOUTUBE_H */
