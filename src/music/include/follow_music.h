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

#ifndef FOLLOW_MUSIC_PUBLIC_H
#define FOLLOW_MUSIC_PUBLIC_H

/* follow::music 的公开 API。
 *
 * 这一层是「音乐」本身：解码播放、歌词、元数据。它不画任何东西，
 * UI 通过下面的接口取数据：
 *   media       miniaudio + FFmpeg 播放引擎
 *   lyrics      LRC 解析
 *   lyricsrc    在线歌词源（netease / qq）
 *   metadata    MusicBrainz 元数据编排 + 本地 SQLite 缓存
 *
 * 依赖：follow::net、follow::core。 */

#include "lyrics.h"
#include "lyricsrc.h"
#include "media.h"
#include "metadata.h"

#endif /* FOLLOW_MUSIC_PUBLIC_H */
