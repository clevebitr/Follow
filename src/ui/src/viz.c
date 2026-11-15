/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

/*
 * 音频粒子背景。
 *
 *   scope   ——  media.c 的音频回调把输出下混成单声道写进一个小环
 *   analyze ——  每帧取最近 1024 个样本，Hann 窗 + 自带的 radix-2 FFT，
 *               分成 24 个对数频段，再做快起慢落的包络
 *   draw    ——  粒子画在动态背景的小纹理里（scrim 之前），所以不会压住文字
 *
 * 三种预设都是为了“背景”而不是“主角”设计的：低透明度、慢速、颜色取自封面。
 * 不依赖 libvisual / OpenGL，也不引入 FFTW。
 */

#include "viz.h"

#include <math.h>
#include <string.h>

#include "media.h"
#include "palette.h"
#include "theme.h"
#include "ui.h"

#define VIZ_PI        3.14159265358979323846
#define VIZ_FFT_N     1024
#define VIZ_BANDS     24
#define VIZ_TRAIL     8
#define VIZ_MAX_PARTS 400

typedef enum {
    VIZ_OFF = 0,
    VIZ_DUST,
    VIZ_WARP,
    VIZ_FLOW
} VizEffect;

typedef struct {
    float x, y;
    float vx, vy;
    float life, max_life;
    float size;
    float alpha;
    float tint;
    float phase;
    float depth;
    float angle;
    float speed;
    float trail_x[VIZ_TRAIL];
    float trail_y[VIZ_TRAIL];
    int   trail_head;
} VizParticle;

static struct {
    VizEffect effect;
    int       count;
    double    gain;
    double    time;
    gint64    last_tick_us;
    guint32   rng;

    float     band[VIZ_BANDS];
    float     bass, mid, treble, level;
    float     bass_avg;
    float     beat, flash;
    double    cooldown;

    VizParticle particles[VIZ_MAX_PARTS];
    int         burst_cursor;
} g;

static float g_window[VIZ_FFT_N];
static float g_re[VIZ_FFT_N];
static float g_im[VIZ_FFT_N];
static int   g_band_lo[VIZ_BANDS];
static int   g_band_hi[VIZ_BANDS];
static int   g_band_rate = 0;
static gboolean g_analyzer_ready = FALSE;

/* ---------------- 小工具 ---------------- */

static double frand(void)
{
    g.rng ^= g.rng << 13;
    g.rng ^= g.rng >> 17;
    g.rng ^= g.rng << 5;

    return (double)(g.rng >> 8) / 16777216.0;
}

static double clamp01(double v)
{
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

/* ---------------- FFT / 频段 ---------------- */

static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * VIZ_PI / (double)len;
        double wr  = cos(ang);
        double wi  = sin(ang);

        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;

            for (int j = 0; j < len / 2; j++) {
                int   a = i + j;
                int   b = i + j + len / 2;
                float tr = (float)(re[b] * cr - im[b] * ci);
                float ti = (float)(re[b] * ci + im[b] * cr);

                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;

                {
                    double nr = cr * wr - ci * wi;

                    ci = cr * wi + ci * wr;
                    cr = nr;
                }
            }
        }
    }
}

static void analyzer_init(int rate)
{
    if (rate <= 0)
        rate = MEDIA_FALLBACK_RATE;

    if (g_analyzer_ready && g_band_rate == rate)
        return;

    for (int i = 0; i < VIZ_FFT_N; i++)
        g_window[i] = (float)(0.5 - 0.5 * cos(2.0 * VIZ_PI * (double)i
                                             / (double)(VIZ_FFT_N - 1)));

    /* 40 Hz → 12 kHz，对数分频。低频段窄一点，镲片/齿音也能看见。 */
    for (int b = 0; b < VIZ_BANDS; b++) {
        double f0 = 40.0 * pow(12000.0 / 40.0, (double)b / (double)VIZ_BANDS);
        double f1 = 40.0 * pow(12000.0 / 40.0, (double)(b + 1) / (double)VIZ_BANDS);
        int    lo = (int)floor(f0 * (double)VIZ_FFT_N / (double)rate);
        int    hi = (int)ceil(f1 * (double)VIZ_FFT_N / (double)rate);

        lo = CLAMP(lo, 1, VIZ_FFT_N / 2 - 1);
        hi = CLAMP(hi, lo, VIZ_FFT_N / 2 - 1);

        g_band_lo[b] = lo;
        g_band_hi[b] = hi;
    }

    g_band_rate      = rate;
    g_analyzer_ready = TRUE;
}

static void analyzer_run(gboolean playing, double dt)
{
    float samples[VIZ_FFT_N];
    float target[VIZ_BANDS];
    int   rate;

    media_read_scope(&app.media, samples, VIZ_FFT_N);
    rate = (int)app.media.sample_rate;

    analyzer_init(rate);

    if (!playing) {
        memset(target, 0, sizeof(target));
    } else {
        double mean = 0.0;

        for (int i = 0; i < VIZ_FFT_N; i++)
            mean += samples[i];
        mean /= (double)VIZ_FFT_N;

        for (int i = 0; i < VIZ_FFT_N; i++) {
            g_re[i] = (float)((samples[i] - mean) * g_window[i]);
            g_im[i] = 0.0f;
        }

        fft(g_re, g_im, VIZ_FFT_N);

        for (int b = 0; b < VIZ_BANDS; b++) {
            double sum = 0.0;
            int    n   = 0;

            for (int k = g_band_lo[b]; k <= g_band_hi[b]; k++) {
                double re = g_re[k];
                double im = g_im[k];

                sum += sqrt(re * re + im * im);
                n++;
            }

            if (n > 0)
                sum /= (double)n;

            /* 归一化后大约落在 0.001..0.5；log 之后小信号也能看见 */
            target[b] = (float)clamp01(log10(1.0 + sum * 8.0) * 2.25 * g.gain);
        }
    }

    for (int b = 0; b < VIZ_BANDS; b++) {
        float a = target[b] > g.band[b] ? 0.62f : 0.16f;

        g.band[b] += (target[b] - g.band[b]) * a;
    }

    {
        double b = 0.0, m = 0.0, t = 0.0;

        for (int i = 0; i < 8; i++)  b += g.band[i];
        for (int i = 8; i < 16; i++) m += g.band[i];
        for (int i = 16; i < VIZ_BANDS; i++) t += g.band[i];

        g.bass   = (float)(b / 8.0);
        g.mid    = (float)(m / 8.0);
        g.treble = (float)(t / 8.0);
        g.level  = (float)(g.bass * 0.55 + g.mid * 0.30 + g.treble * 0.15);
    }

    if (playing) {
        g.bass_avg += (g.bass - g.bass_avg) * (g.bass > g.bass_avg ? 0.09f : 0.015f);

        if (g.cooldown > 0.0)
            g.cooldown -= dt;

        if (g.bass > g.bass_avg * 1.28f + 0.018f && g.cooldown <= 0.0) {
            g.beat    = 1.0f;
            g.flash   = 1.0f;
            g.cooldown = 0.16;
        }
    } else {
        g.bass_avg *= 0.96f;
    }

    g.beat  *= (float)exp(-dt * 5.0);
    g.flash *= (float)exp(-dt * 3.2);
}

/* ---------------- 粒子 ---------------- */

static void trail_reset(VizParticle *p)
{
    for (int i = 0; i < VIZ_TRAIL; i++) {
        p->trail_x[i] = p->x;
        p->trail_y[i] = p->y;
    }

    p->trail_head = 0;
}

static void particle_spawn(VizParticle *p, gboolean initial)
{
    p->life     = 6.0f + (float)frand() * 8.0f;
    p->max_life = p->life;
    p->size     = 0.006f + (float)frand() * 0.014f;
    p->alpha    = 0.30f + (float)frand() * 0.45f;
    p->tint     = (float)frand();
    p->phase    = (float)(frand() * VIZ_PI * 2.0);
    p->depth    = 0.35f + (float)frand() * 0.65f;
    p->vx       = (float)((frand() - 0.5) * 0.012);
    p->vy       = -(0.006f + (float)frand() * 0.020f);
    p->angle    = (float)(frand() * VIZ_PI * 2.0);
    p->speed    = 0.04f + (float)frand() * 0.10f;

    switch (g.effect) {
    case VIZ_WARP: {
        double radius = initial ? sqrt(frand()) * 0.22 : 0.0;
        double angle  = frand() * VIZ_PI * 2.0;

        p->x     = (float)(0.5 + cos(angle) * radius);
        p->y     = (float)(0.5 + sin(angle) * radius);
        p->angle = (float)angle;
        p->speed = 0.14f + (float)frand() * 0.30f;
        p->size  = 0.002f + (float)frand() * 0.004f;
        p->alpha = 0.25f + (float)frand() * 0.45f;
        p->life  = p->max_life = 1.2f + (float)frand() * 2.2f;
        break;
    }
    case VIZ_FLOW:
        p->x    = (float)frand();
        p->y    = (float)frand();
        p->size = 0.002f + (float)frand() * 0.005f;
        p->life = p->max_life = 4.0f + (float)frand() * 7.0f;
        p->speed = 0.020f + (float)frand() * 0.040f;
        break;
    case VIZ_DUST:
    default:
        p->x = (float)frand();
        p->y = initial ? (float)frand() : 1.04f + (float)frand() * 0.08f;
        p->vy = -(0.006f + (float)frand() * 0.020f);
        break;
    }

    trail_reset(p);
}

static void particles_init(void)
{
    for (int i = 0; i < g.count; i++)
        particle_spawn(&g.particles[i], TRUE);
}

static void particle_step(VizParticle *p, double dt)
{
    float out_margin = 0.10f;

    p->life -= (float)dt;

    switch (g.effect) {
    case VIZ_DUST: {
        double sway = sin((double)p->y * 7.0 + g.time * 0.55 + p->phase);
        double lift = (double)g.bass * 0.060 * p->depth;
        double speed = 0.55 + (double)p->depth;

        p->x += (float)((p->vx + sway * 0.004 * (0.4 + g.mid)) * dt * speed);
        p->y += (float)(((double)p->vy - lift) * dt * speed);

        if (p->x < -out_margin) p->x = 1.0f + out_margin;
        if (p->x > 1.0f + out_margin) p->x = -out_margin;

        /* 飘出顶部就回收，否则粒子会越飘越少 */
        if (p->y < -out_margin)
            p->life = 0.0f;
        break;
    }
    case VIZ_WARP: {
        double mul = 0.45 + (double)g.bass * 2.4 + (double)g.beat * 1.8;

        p->x += (float)(cos(p->angle) * p->speed * mul * dt);
        p->y += (float)(sin(p->angle) * p->speed * mul * dt);

        if (p->x < -0.2f || p->x > 1.2f || p->y < -0.2f || p->y > 1.2f)
            p->life = 0.0f;
        break;
    }
    case VIZ_FLOW: {
        double a = 2.0 * VIZ_PI * (sin((double)p->x * 3.2 + g.time * 0.21)
                                 + cos((double)p->y * 4.1 - g.time * 0.17));
        double s = (double)p->speed + (double)g.mid * 0.060 + (double)g.bass * 0.025;

        p->x += (float)(cos(a) * s * dt);
        p->y += (float)(sin(a) * s * dt);

        {
            gboolean wrapped = FALSE;

            if (p->x < -0.05f) { p->x = 1.05f; wrapped = TRUE; }
            if (p->x > 1.05f)  { p->x = -0.05f; wrapped = TRUE; }
            if (p->y < -0.05f) { p->y = 1.05f; wrapped = TRUE; }
            if (p->y > 1.05f)  { p->y = -0.05f; wrapped = TRUE; }

            /* 粒子从一边绕到另一边时，旧轨迹要清掉，否则会拉出一条横贯屏幕的直线 */
            if (wrapped)
                trail_reset(p);
        }
        break;
    }
    default:
        break;
    }

    p->trail_head = (p->trail_head + 1) % VIZ_TRAIL;
    p->trail_x[p->trail_head] = p->x;
    p->trail_y[p->trail_head] = p->y;

    if (p->life <= 0.0f)
        particle_spawn(p, FALSE);
}

/* ---------------- 配置 / 生命周期 ---------------- */

static VizEffect effect_from_name(const gchar *name)
{
    if (name == NULL || *name == '\0')
        return VIZ_DUST;
    if (g_ascii_strcasecmp(name, "off") == 0 ||
        g_ascii_strcasecmp(name, "none") == 0)
        return VIZ_OFF;
    if (g_ascii_strcasecmp(name, "warp") == 0 ||
        g_ascii_strcasecmp(name, "starfield") == 0)
        return VIZ_WARP;
    if (g_ascii_strcasecmp(name, "flow") == 0 ||
        g_ascii_strcasecmp(name, "field") == 0)
        return VIZ_FLOW;

    return VIZ_DUST;
}

void viz_configure(const gchar *effect, int particles, double gain)
{
    VizEffect next = effect_from_name(effect);
    int       count = CLAMP(particles, 20, VIZ_MAX_PARTS);

    if (next == g.effect && count == g.count && g.count > 0) {
        g.gain = (isfinite(gain) && gain > 0.0) ? CLAMP(gain, 0.2, 4.0) : 1.0;
        return;
    }

    g.effect = next;
    g.count  = count;
    g.gain   = (isfinite(gain) && gain > 0.0) ? CLAMP(gain, 0.2, 4.0) : 1.0;
    g.rng    = (guint32)(g_get_monotonic_time() & 0xffffffffu) | 1u;

    memset(g.band, 0, sizeof(g.band));
    g.bass = g.mid = g.treble = g.level = 0.0f;
    g.bass_avg = g.beat = g.flash = 0.0f;
    g.cooldown = 0.0;
    g.time     = 0.0;
    g.last_tick_us = g_get_monotonic_time();

    particles_init();
}

void viz_reset(void)
{
    memset(&g, 0, sizeof(g));
    g.effect = VIZ_OFF;
    g.count  = 0;
    g.gain   = 1.0;
    g_analyzer_ready = FALSE;
    g_band_rate = 0;
}

gboolean viz_active(void)
{
    return g.effect != VIZ_OFF && g.count > 0;
}

const gchar *viz_effect_name(void)
{
    switch (g.effect) {
    case VIZ_WARP: return "warp";
    case VIZ_FLOW: return "flow";
    case VIZ_DUST: return "dust";
    default:       return "off";
    }
}

void viz_burst(void)
{
    if (!viz_active())
        return;

    for (int i = 0; i < 6; i++) {
        VizParticle *p = &g.particles[(g.burst_cursor++) % g.count];

        particle_spawn(p, TRUE);
        p->alpha = MIN(0.85f, p->alpha + 0.18f);
        p->life  = p->max_life;
    }
}

void viz_tick(void)
{
    gint64 now;
    double dt;

    if (!viz_active())
        return;

    now = g_get_monotonic_time();
    dt  = (double)(now - g.last_tick_us) / 1000000.0;
    g.last_tick_us = now;
    dt = CLAMP(dt, 0.0, 0.12);
    g.time += dt;

    analyzer_run(media_is_playing(&app.media), dt);

    for (int i = 0; i < g.count; i++)
        particle_step(&g.particles[i], dt);
}

/* ---------------- 绘制 ---------------- */

static void particle_color(const Palette *pal, float tint, double *r, double *g_, double *b)
{
    if (pal != NULL && pal->has) {
        double s = (double)tint;

        if (s < 0.58) {
            double t = s / 0.58;

            *r = pal->seed_r + (pal->alt_r - pal->seed_r) * t;
            *g_ = pal->seed_g + (pal->alt_g - pal->seed_g) * t;
            *b = pal->seed_b + (pal->alt_b - pal->seed_b) * t;
        } else {
            double t = (s - 0.58) / 0.42;

            *r = pal->alt_r + (0.90 - pal->alt_r) * t;
            *g_ = pal->alt_g + (0.94 - pal->alt_g) * t;
            *b = pal->alt_b + (1.00 - pal->alt_b) * t;
        }
    } else {
        *r = 0.62;
        *g_ = 0.76;
        *b = 0.96;
    }

    /* 往白色提一点，保证是“发光”而不是“脏色” */
    *r = *r * 0.72 + 0.28;
    *g_ = *g_ * 0.72 + 0.30;
    *b = *b * 0.72 + 0.34;
}

static double particle_fade(const VizParticle *p)
{
    double age = (double)(p->max_life - p->life);
    double in  = MIN(1.0, age / 0.55);
    double out = MIN(1.0, (double)p->life / 0.85);

    return clamp01(in * out);
}

static void draw_dot(cairo_t *cr, double x, double y, double radius, double alpha,
                     double r, double g_, double b)
{
    if (alpha <= 0.001 || radius <= 0.0)
        return;

    /* 用平滑的径向渐变而不是硬边圆：小纹理放大到全屏时，
     * 硬边圆会被高质量缩放滤镜拉出“甜甜圈”振铃。 */
    cairo_pattern_t *pat = cairo_pattern_create_radial(x, y, 0.0, x, y, radius * 2.6);

    cairo_pattern_add_color_stop_rgba(pat, 0.00, r, g_, b, alpha);
    cairo_pattern_add_color_stop_rgba(pat, 0.18, r, g_, b, alpha * 0.62);
    cairo_pattern_add_color_stop_rgba(pat, 0.42, r, g_, b, alpha * 0.22);
    cairo_pattern_add_color_stop_rgba(pat, 0.70, r, g_, b, alpha * 0.05);
    cairo_pattern_add_color_stop_rgba(pat, 1.00, r, g_, b, 0.0);

    cairo_set_source(cr, pat);
    cairo_arc(cr, x, y, radius * 2.6, 0.0, VIZ_PI * 2.0);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
}

static void draw_flow(cairo_t *cr, const VizParticle *p, double w, double h,
                      double unit, double alpha, double r, double g_, double b)
{
    int     head = p->trail_head;
    gboolean started = FALSE;
    double  px = 0.0, py = 0.0;

    for (int i = VIZ_TRAIL - 1; i >= 0; i--) {
        int    idx = (head - i + VIZ_TRAIL) % VIZ_TRAIL;
        double x   = (double)p->trail_x[idx] * w;
        double y   = (double)p->trail_y[idx] * h;
        double t   = (double)(VIZ_TRAIL - i) / (double)VIZ_TRAIL;
        double a   = alpha * t * 0.55;

        if (!started) {
            px = x;
            py = y;
            started = TRUE;
            continue;
        }

        cairo_set_source_rgba(cr, r, g_, b, a);
        cairo_set_line_width(cr, MAX(0.45, unit * (double)p->size * (0.30 + t * 0.65)));
        cairo_move_to(cr, px, py);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);

        px = x;
        py = y;
    }

    draw_dot(cr, (double)p->x * w, (double)p->y * h, unit * (double)p->size * 0.9,
             alpha, r, g_, b);
}

void viz_draw(cairo_t *cr, double w, double h, const Palette *pal)
{
    double unit;

    if (!viz_active() || w <= 0.0 || h <= 0.0)
        return;

    unit = MIN(w, h);

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    if (g.effect == VIZ_WARP) {
        double cx = w * 0.5;
        double cy = h * 0.5;
        double rr = unit * (0.10 + 0.14 * (double)g.bass + 0.08 * (double)g.flash);
        double r, gg, b;

        particle_color(pal, 0.35f, &r, &gg, &b);

        cairo_pattern_t *glow = cairo_pattern_create_radial(cx, cy, 0.0, cx, cy, rr);

        cairo_pattern_add_color_stop_rgba(glow, 0.0, r, gg, b,
                                          0.10 + 0.18 * (double)g.bass);
        cairo_pattern_add_color_stop_rgba(glow, 0.45, r, gg, b,
                                          0.04 + 0.08 * (double)g.bass);
        cairo_pattern_add_color_stop_rgba(glow, 1.0, r, gg, b, 0.0);
        cairo_set_source(cr, glow);
        cairo_paint(cr);
        cairo_pattern_destroy(glow);
    }

    for (int i = 0; i < g.count; i++) {
        const VizParticle *p = &g.particles[i];
        double r, gg, b;
        double fade = particle_fade(p);
        double x    = (double)p->x * w;
        double y    = (double)p->y * h;
        double size = unit * (double)p->size;
        double a;

        if (fade <= 0.0)
            continue;

        particle_color(pal, p->tint, &r, &gg, &b);

        switch (g.effect) {
        case VIZ_DUST: {
            double twinkle = 0.72 + 0.28 * sin(g.time * 1.7 + (double)p->phase);

            a = (double)p->alpha * fade * twinkle
              * (0.62 + 0.55 * (double)g.level);

            draw_dot(cr, x, y, size, a, r, gg, b);
            break;
        }
        case VIZ_WARP: {
            int    prev = (p->trail_head - 1 + VIZ_TRAIL) % VIZ_TRAIL;
            double px   = (double)p->trail_x[prev] * w;
            double py   = (double)p->trail_y[prev] * h;

            a = (double)p->alpha * fade * (0.40 + 0.75 * (double)g.bass);

            cairo_set_source_rgba(cr, r, gg, b, a);
            cairo_set_line_width(cr, MAX(0.6, size * 0.85));
            cairo_move_to(cr, px, py);
            cairo_line_to(cr, x, y);
            cairo_stroke(cr);

            draw_dot(cr, x, y, MAX(0.35, size * 0.45), a * 0.9, r, gg, b);
            break;
        }
        case VIZ_FLOW:
            a = (double)p->alpha * fade * (0.35 + 0.65 * (double)g.mid);

            draw_flow(cr, p, w, h, unit, a, r, gg, b);
            break;
        default:
            break;
        }
    }

    cairo_restore(cr);
}

/* ---------------- GPU sprite 顶点 ---------------- */

static void emit_sprite(VizGlVertex *out, int *n, int max,
                        double cx, double cy, double radius,
                        double r, double g_, double b, double a)
{
    double half;

    if (out == NULL || radius <= 0.0 || a <= 0.001)
        return;

    if (*n + 6 > max)
        return;

    /* 和 draw_dot 一致：可见核心是 radius，径向渐变铺到 2.6 倍。 */
    half = radius * 2.6;

    {
        const double x0 = cx - half, y0 = cy - half;
        const double x1 = cx + half, y1 = cy + half;
        const VizGlVertex verts[6] = {
            { (float)x0, (float)y0, 0.0f, 0.0f, (float)r, (float)g_, (float)b, (float)a },
            { (float)x1, (float)y0, 1.0f, 0.0f, (float)r, (float)g_, (float)b, (float)a },
            { (float)x0, (float)y1, 0.0f, 1.0f, (float)r, (float)g_, (float)b, (float)a },
            { (float)x1, (float)y0, 1.0f, 0.0f, (float)r, (float)g_, (float)b, (float)a },
            { (float)x1, (float)y1, 1.0f, 1.0f, (float)r, (float)g_, (float)b, (float)a },
            { (float)x0, (float)y1, 0.0f, 1.0f, (float)r, (float)g_, (float)b, (float)a },
        };

        memcpy(out + *n, verts, sizeof(verts));
        *n += 6;
    }
}

int viz_gl_vertices(VizGlVertex *out, int max, double w, double h,
                    const Palette *pal)
{
    int    n = 0;
    double unit;

    if (!viz_active() || out == NULL || max < 6 || w <= 0.0 || h <= 0.0)
        return 0;

    unit = MIN(w, h);

    if (g.effect == VIZ_WARP) {
        double cx = w * 0.5;
        double cy = h * 0.5;
        double rr = unit * (0.10 + 0.14 * (double)g.bass + 0.08 * (double)g.flash);
        double r, gg, b;

        particle_color(pal, 0.35f, &r, &gg, &b);
        emit_sprite(out, &n, max, cx, cy, rr * 0.40,
                    r, gg, b, 0.10 + 0.18 * (double)g.bass);
    }

    for (int i = 0; i < g.count; i++) {
        const VizParticle *p = &g.particles[i];
        double r, gg, b;
        double fade = particle_fade(p);
        double x    = (double)p->x * w;
        double y    = (double)p->y * h;
        double size = unit * (double)p->size;
        double a;

        if (fade <= 0.0)
            continue;

        particle_color(pal, p->tint, &r, &gg, &b);

        switch (g.effect) {
        case VIZ_DUST: {
            double twinkle = 0.72 + 0.28 * sin(g.time * 1.7 + (double)p->phase);

            a = (double)p->alpha * fade * twinkle
              * (0.62 + 0.55 * (double)g.level);

            emit_sprite(out, &n, max, x, y, size, r, gg, b, a);
            break;
        }
        case VIZ_WARP: {
            int    prev = (p->trail_head - 1 + VIZ_TRAIL) % VIZ_TRAIL;
            double px   = (double)p->trail_x[prev] * w;
            double py   = (double)p->trail_y[prev] * h;

            a = (double)p->alpha * fade * (0.40 + 0.75 * (double)g.bass);

            /* 用两点 + 中点拼一条发光短拖尾，避免 GPU 路径再开一个线段 shader。 */
            emit_sprite(out, &n, max, px, py, MAX(0.35, size * 0.55),
                        r, gg, b, a * 0.55);
            emit_sprite(out, &n, max, (px + x) * 0.5, (py + y) * 0.5,
                        MAX(0.30, size * 0.50), r, gg, b, a * 0.75);
            emit_sprite(out, &n, max, x, y, MAX(0.35, size * 0.45),
                        r, gg, b, a * 0.9);
            break;
        }
        case VIZ_FLOW: {
            int head = p->trail_head;

            a = (double)p->alpha * fade * (0.35 + 0.65 * (double)g.mid);

            for (int k = VIZ_TRAIL - 1; k >= 0; k--) {
                int    idx = (head - k + VIZ_TRAIL) % VIZ_TRAIL;
                double tx  = (double)p->trail_x[idx] * w;
                double ty  = (double)p->trail_y[idx] * h;
                double t   = (double)(VIZ_TRAIL - k) / (double)VIZ_TRAIL;
                double tr  = unit * (double)p->size * (0.55 + t * 0.55);

                emit_sprite(out, &n, max, tx, ty, tr, r, gg, b, a * t * 0.55);
            }

            emit_sprite(out, &n, max, x, y, unit * (double)p->size * 0.9,
                        r, gg, b, a);
            break;
        }
        default:
            break;
        }
    }

    return n;
}
