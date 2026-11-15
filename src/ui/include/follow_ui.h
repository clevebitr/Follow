/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性。内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_UI_PUBLIC_H
#define FOLLOW_UI_PUBLIC_H

/* follow::ui 的公开 API。
 *
 * GTK4 + cairo + OpenGL 的全部界面都在这里：主画面、歌曲墙、歌词层、
 * 动态背景、命令面板、性能面板，以及新的歌曲详情页。
 *
 * 其它模块（尤其是 app）只通过 ui.h / details.h 这些入口和 UI 打交道，
 * 不直接操作 GtkWidget。 */

#include "cover.h"
#include "details.h"
#include "player.h"
#include "playlist.h"
#include "stage.h"
#include "ui.h"

#endif /* FOLLOW_UI_PUBLIC_H */
