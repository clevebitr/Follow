#include "fx.h"

void fx_begin(Fx *fx, double duration_sec)
{
    fx_begin_later(fx, duration_sec, 0.0);
}

void fx_begin_later(Fx *fx, double duration_sec, double delay_sec)
{
    fx->active     = TRUE;
    fx->started_us = g_get_monotonic_time();
    fx->delay      = MAX(delay_sec, 0.0);
    fx->duration   = MAX(duration_sec, 0.01);
}

void fx_stop(Fx *fx)
{
    fx->active = FALSE;
}

gboolean fx_running(Fx *fx)
{
    return fx != NULL && fx->active;
}

double fx_t(Fx *fx)
{
    double x, t;

    if (fx == NULL || !fx->active)
        return 1.0;

    x = (double)(g_get_monotonic_time() - fx->started_us) / 1000000.0 - fx->delay;

    if (x <= 0.0)
        return 0.0;                    /* 还在延迟里：画面保持原样 */

    x /= fx->duration;

    if (x >= 1.0) {
        fx->active = FALSE;
        return 1.0;
    }

    t = 1.0 - (1.0 - x) * (1.0 - x) * (1.0 - x);   /* ease-out cubic */

    return t;
}
