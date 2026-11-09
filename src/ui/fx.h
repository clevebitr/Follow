#ifndef FOLLOW_FX_H
#define FOLLOW_FX_H

#include <glib.h>

/* 极简过渡：只记「什么时候开始 + 多久」，进度按需算。
 * 不保存逐帧状态，所以随时可以被打断重来（连着切歌也不会卡在半路）。 */

typedef struct {
    gboolean active;
    gint64   started_us;   /* g_get_monotonic_time() */
    double   delay;        /* 秒：先按住不动，用来和别的过渡错开 */
    double   duration;     /* 秒 */
} Fx;

void     fx_begin(Fx *fx, double duration_sec);

/* 延迟 delay 秒后再开始（delay 期间进度恒为 0）—— 让组件比背景慢半拍 */
void     fx_begin_later(Fx *fx, double duration_sec, double delay_sec);
void     fx_stop(Fx *fx);
gboolean fx_running(Fx *fx);

/* 0..1 的缓动进度（ease-out cubic：起步快、收尾慢，像潮水退下去）。
 * 到 1 时自动把 fx 置为不活跃。 */
double   fx_t(Fx *fx);

#endif /* FOLLOW_FX_H */
