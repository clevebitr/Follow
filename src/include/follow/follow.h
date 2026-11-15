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

#ifndef FOLLOW_PUBLIC_H
#define FOLLOW_PUBLIC_H

/* Follow 的全部公开模块 API。
 *
 *   follow::core   → follow_core.h
 *   follow::net    → follow_net.h
 *   follow::music  → follow_music.h
 *   follow::ui     → follow_ui.h
 *
 * 分层依赖方向（只允许向下）：
 *
 *   app → ui → music → net → core
 *
 * 每个模块都是一个独立静态库，可以单独编译、单独链接；app 只负责
 * 生命周期、键盘分发和定时器，不再包含业务实现。 */

#include "follow_core.h"
#include "follow_music.h"
#include "follow_net.h"
#include "follow_ui.h"

#endif /* FOLLOW_PUBLIC_H */
