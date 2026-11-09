#include <math.h>
#include <string.h>

#include "palette.h"

#define HUE_BINS 24

typedef struct {
    double weight;
    double r, g, b;
} HueBin;

static void rgb_to_hsv(double r, double g, double b,
                       double *h, double *s, double *v)
{
    double max = MAX(r, MAX(g, b));
    double min = MIN(r, MIN(g, b));
    double d = max - min;

    *v = max;
    *s = max > 0.0 ? d / max : 0.0;

    if (d <= 0.0) {
        *h = 0.0;
        return;
    }

    if (max == r)      *h = fmod((g - b) / d, 6.0);
    else if (max == g) *h = (b - r) / d + 2.0;
    else               *h = (r - g) / d + 4.0;

    *h /= 6.0;
    if (*h < 0.0)
        *h += 1.0;
}

Palette palette_mix(const Palette *a, const Palette *b, double t)
{
    Palette out;

    palette_reset(&out);

    if (a == NULL || b == NULL)
        return a != NULL ? *a : (b != NULL ? *b : out);

    t = CLAMP(t, 0.0, 1.0);

#define MIX(field) out.field = a->field + (b->field - a->field) * t
    MIX(seed_r); MIX(seed_g); MIX(seed_b);
    MIX(alt_r);  MIX(alt_g);  MIX(alt_b);
    MIX(deep_r); MIX(deep_g); MIX(deep_b);
#undef MIX

    out.has = a->has || b->has;
    return out;
}

void palette_reset(Palette *p)
{
    if (p == NULL)
        return;

    memset(p, 0, sizeof(*p));
}

void palette_from_rgba(const guchar *rgba, int width, int height, Palette *out)
{
    HueBin bins[HUE_BINS];
    int    step;
    int    best = -1, second = -1;
    double best_weight = 0.0, second_weight = 0.0;

    if (out == NULL)
        return;

    palette_reset(out);

    /* 没有封面时给一组中性色，让背景不至于死黑 */
    out->seed_r = 0.24; out->seed_g = 0.26; out->seed_b = 0.34;
    out->alt_r  = 0.16; out->alt_g  = 0.18; out->alt_b  = 0.26;
    out->deep_r = 0.08; out->deep_g = 0.09; out->deep_b = 0.13;

    if (rgba == NULL || width <= 0 || height <= 0)
        return;

    memset(bins, 0, sizeof(bins));

    step = MAX(1, MAX(width, height) / 96);   /* 抽样到 ~96px 就够 */

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            const guchar *px = rgba + ((gsize)y * (gsize)width + (gsize)x) * 4;
            double r = px[0] / 255.0;
            double g = px[1] / 255.0;
            double b = px[2] / 255.0;
            double h, s, v, w;
            int    bin;

            if (px[3] < 8)
                continue;

            rgb_to_hsv(r, g, b, &h, &s, &v);

            /* 灰、黑、过曝的像素不参与取色；用 s² 加权让"真有颜色"的像素说话 */
            if (s < 0.15 || v < 0.12)
                continue;

            w   = s * s * v;
            bin = (int)(h * HUE_BINS) % HUE_BINS;

            bins[bin].weight += w;
            bins[bin].r      += r * w;
            bins[bin].g      += g * w;
            bins[bin].b      += b * w;
        }
    }

    for (int i = 0; i < HUE_BINS; i++) {
        if (bins[i].weight > best_weight) {
            best_weight = bins[i].weight;
            best        = i;
        }
    }

    if (best < 0 || best_weight <= 0.0)
        return;      /* 灰度封面：保持中性色 */

    /* 第二色：色相至少隔开 4 个桶，避免两个色太像 */
    for (int i = 0; i < HUE_BINS; i++) {
        int d = abs(i - best);

        if (d < 4 || d > HUE_BINS - 4)
            continue;

        if (bins[i].weight > second_weight) {
            second_weight = bins[i].weight;
            second        = i;
        }
    }

    out->seed_r = bins[best].r / best_weight;
    out->seed_g = bins[best].g / best_weight;
    out->seed_b = bins[best].b / best_weight;

    if (second >= 0 && second_weight > 0.0) {
        out->alt_r = bins[second].r / second_weight;
        out->alt_g = bins[second].g / second_weight;
        out->alt_b = bins[second].b / second_weight;
    } else {
        out->alt_r = out->seed_r * 0.7;
        out->alt_g = out->seed_g * 0.7;
        out->alt_b = out->seed_b * 0.7;
    }

    /* 取出来的色如果本身也不够鲜艳，就当成灰度封面，别给背景染上一层怪颜色 */
    {
        double h, s, v;

        rgb_to_hsv(out->seed_r, out->seed_g, out->seed_b, &h, &s, &v);

        if (s < 0.20 || v < 0.06) {
            double sr = out->seed_r, sg = out->seed_g, sb = out->seed_b;

            palette_reset(out);
            out->seed_r = (sr + out->seed_r) * 0.5;
            out->seed_g = (sg + out->seed_g) * 0.5;
            out->seed_b = (sb + out->seed_b) * 0.5;
            return;
        }
    }

    out->deep_r = out->seed_r * 0.32;
    out->deep_g = out->seed_g * 0.32;
    out->deep_b = out->seed_b * 0.32;

    out->has = TRUE;
}
