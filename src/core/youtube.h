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

/* --------------------------------------------------------------------------
 * ⚠ 法律风险提示
 *
 * 这里调用的是 YouTube 网页 / App 自己用的 InnerTube 接口
 * （/youtubei/v1/player），并伪装成官方客户端（UA + clientName/Version）。
 * 该接口没有公开文档，YouTube 服务条款也不允许第三方程序绕过播放器取流。
 * 仅供个人学习与技术研究：只做在线播放，不下载、不保存、不再分发内容；
 * 接口随时可能变更或封禁。中国大陆直连不可用，需要 [net] proxy。
 * -------------------------------------------------------------------------- */

#ifndef FOLLOW_YOUTUBE_H
#define FOLLOW_YOUTUBE_H

#include <glib.h>

/* 从任意 YouTube 链接里取 11 位 videoId：
 *   youtube.com/watch?v=ID        music.youtube.com/watch?v=ID&list=...
 *   youtu.be/ID                   youtube.com/shorts/ID
 *   youtube.com/live/ID           youtube.com/embed/ID
 * 或者直接给一个光秃秃的 ID。取不到返回 NULL。 */
gchar *youtube_video_id(const gchar *url);

/* 解析出一条可播放的音频地址（googlevideo 直链）。
 * 成功返回 0，并把 title（视频标题）/ artist（频道名）填上（调用方 g_free）；
 * 失败返回 -1，*error 里是给人看的原因（需要代理 / 需要登录 / 纯加密流 ...）。 */
int youtube_resolve(const char *page_url, gchar **audio_url,
                    gchar **title, gchar **artist, gchar **error);

/* 播放 googlevideo 直链时要带的请求头（User-Agent 等），g_free 释放 */
gchar *youtube_request_headers(void);

#endif /* FOLLOW_YOUTUBE_H */
