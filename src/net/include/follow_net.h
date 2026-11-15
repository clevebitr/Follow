/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_NET_PUBLIC_H
#define FOLLOW_NET_PUBLIC_H

/* follow::net 的公开 API。
 *
 * 这一层只做「和外面说话」：HTTP 传输、平台页面解析、公开数据库查询。
 * 它不碰 GTK 控件，也不碰播放状态，所以可以单独编译 / 单独测试：
 *   http          libcurl 包装（GET / POST / 额外请求头）
 *   httpstream    给 FFmpeg 用的自定义 AVIO（Range 分块读）
 *   source        把 ini 里的 link 解析成可播放直链
 *   resolvecache  解析结果落盘缓存
 *   bilibili      B 站页面 → 音频直链
 *   youtube       YouTube 页面 → InnerTube 直链
 *   musicbrainz   MusicBrainz / Cover Art Archive 查询
 *
 * 依赖：follow::core（config / cache / platform）。 */

#include "bilibili.h"
#include "http.h"
#include "httpstream.h"
#include "musicbrainz.h"
#include "resolvecache.h"
#include "source.h"
#include "youtube.h"

#endif /* FOLLOW_NET_PUBLIC_H */
