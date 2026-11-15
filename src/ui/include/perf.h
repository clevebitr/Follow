/**
 * created: 2026/11/12
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/12
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */
#ifndef FOLLOW_PERF_H
#define FOLLOW_PERF_H

#include <gtk/gtk.h>

/* 性能面板（F12）：帧率 / 帧间隔与各层 CPU 耗时。 */

typedef enum {
    PERF_PHASE_STAGE = 0,
    PERF_PHASE_CHROME,
    PERF_PHASE_LYRIC,
    PERF_PHASE_WALL,
    PERF_PHASE_BACKDROP_GL,
    PERF_PHASE_BACKDROP_CPU,
    PERF_PHASE_COUNT
} PerfPhase;

GtkWidget *perf_new(GtkWidget *window);

void perf_clear(void);

void     perf_toggle(void);
gboolean perf_visible(void);

void perf_phase_begin(PerfPhase phase);
void perf_phase_end(PerfPhase phase);

void perf_note_upload(gsize bytes);

#endif
