/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#ifndef FOLLOW_FX_H
#define FOLLOW_FX_H

#include <glib.h>

/* 过渡 */

typedef struct {
    gboolean active;
    gint64   started_us;   /* g_get_monotonic_time() */
    double   delay;        /* 秒：先按住不动，用来和别的过渡错开 */
    double   duration;     /* 秒 */
} Fx;

void     fx_begin(Fx *fx, double duration_sec);

/* 延迟 delay 秒后再开始 */
void     fx_begin_later(Fx *fx, double duration_sec, double delay_sec);
void     fx_stop(Fx *fx);
gboolean fx_running(Fx *fx);

/* 0..1 的缓动进度 */
double   fx_t(Fx *fx);

#endif /* FOLLOW_FX_H */
