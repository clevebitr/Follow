/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_PUBLIC_H
#define FOLLOW_PUBLIC_H

/* Follow 公开模块 API。依赖方向：app → ui → music → net → core。
 * 每个模块均为独立静态库，可单独编译与链接。 */

#include "follow_core.h"
#include "follow_music.h"
#include "follow_net.h"
#include "follow_ui.h"

#endif
