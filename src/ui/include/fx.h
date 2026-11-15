/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_FX_H
#define FOLLOW_FX_H

#include <glib.h>

typedef struct {
    gboolean active;
    gint64   started_us;
    double   delay;
    double   duration;
} Fx;

void     fx_begin(Fx *fx, double duration_sec);

void     fx_begin_later(Fx *fx, double duration_sec, double delay_sec);
void     fx_stop(Fx *fx);
gboolean fx_running(Fx *fx);

double   fx_t(Fx *fx);

#endif
