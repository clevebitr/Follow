/**
 * created: 2026/11/12
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/12
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#include "perf.h"

#include <math.h>
#include <string.h>

#include "backdropgl.h"
#include "lyricgl.h"
#include "theme.h"
#include "uidbg.h"
#include "ui.h"
#include "wallgl.h"

#define PERF_RING        240       /* 帧间隔环形缓冲：60fps 约 4 秒 */
#define PERF_SPARK       120       /* 面板上画的柱子数 */
#define PERF_IDLE_GAP    100000    /* > 100ms 的间隔算空闲，不进统计 */
#define PERF_WINDOW_US   2000000   /* 帧率统计窗口：最近 2 秒 */
#define PERF_UPDATE_MS   200       /* 面板刷新间隔 */
#define PERF_ROTATE_US   950000    /* 每秒折算一次各层调用频率 */

#define PERF_PANEL_W     344.0
#define PERF_PAD         14.0
#define PERF_ROW_H       15.0

/* 面板上的行：把两个 phase 合成一行（背景 GL / CPU 回退共用"bg"） */
#define PERF_ROWS        5

typedef struct {
    gint64 at_us;
    gint64 gap_us;
} Gap;

typedef struct {
    double sum_ms;    /* 当前统计窗口内的总耗时 */
    guint  calls;     /* 当前统计窗口内的调用次数 */
} PhaseStat;

static struct {
    GtkWidget     *area;
    GtkWidget     *window;
    GdkFrameClock *clock;
    gboolean       on;
    guint          timer;

    /* 帧 */
    Gap    ring[PERF_RING];
    guint  ring_i;
    guint  ring_n;
    gint64 last_us;
    gint64 frames_total;
    gint64 refresh_us;

    double spark[PERF_SPARK];
    guint  spark_i;
    guint  spark_n;

    /* 各层耗时 */
    gint64    start[PERF_PHASE_COUNT];
    PhaseStat ph[PERF_PHASE_COUNT];
    double    rate[PERF_PHASE_COUNT];   /* 每秒调用次数（上一个窗口） */
    double    ms[PERF_PHASE_COUNT];     /* 每次调用平均耗时 */
    gint64    win_start;
    gsize     upload_bytes;
    double    upload_mbps;

    /* 面板显示用快照 */
    gboolean idle;
    double   fps;
    double   frame_ms;
    double   p95_ms;
    double   worst_ms;
    double   cpu_pct;
} P;

static const Rgba perf_ok   = { 0.62, 0.86, 0.72, 0.90 };
static const Rgba perf_warn = { 0.95, 0.78, 0.45, 0.92 };
static const Rgba perf_bad  = { 0.95, 0.52, 0.52, 0.92 };

/* ---------------- 帧时钟采样 ---------------- */

static void on_after_paint(GdkFrameClock *clock, gpointer data)
{
    GdkFrameTimings *timings;
    gint64           now = g_get_monotonic_time();

    (void)data;

    if (P.last_us > 0) {
        gint64 gap = now - P.last_us;

        P.ring[P.ring_i].at_us  = now;
        P.ring[P.ring_i].gap_us = gap;
        P.ring_i = (P.ring_i + 1) % PERF_RING;

        if (P.ring_n < PERF_RING)
            P.ring_n++;

        /* 空闲期的间隔（比如没东西可画、只有面板自己在刷）不画进柱子，
         * 否则一屏全是几百毫秒的红条，看不出真实负载。 */
        if (gap <= PERF_IDLE_GAP) {
            P.spark[P.spark_i] = (double)gap / 1000.0;
            P.spark_i = (P.spark_i + 1) % PERF_SPARK;

            if (P.spark_n < PERF_SPARK)
                P.spark_n++;
        }
    }

    P.last_us = now;
    P.frames_total++;

    timings = gdk_frame_clock_get_current_timings(clock);
    if (timings != NULL) {
        gint64 refresh = gdk_frame_timings_get_refresh_interval(timings);

        if (refresh > 0)
            P.refresh_us = refresh;
    }
}

static void bind_clock(void)
{
    GdkFrameClock *cur;

    if (P.window == NULL)
        return;

    cur = gtk_widget_get_frame_clock(P.window);

    if (cur == P.clock)
        return;

    if (P.clock != NULL) {
        g_signal_handlers_disconnect_by_func(P.clock,
                                             G_CALLBACK(on_after_paint), NULL);
        g_clear_object(&P.clock);
    }

    if (cur != NULL) {
        P.clock = g_object_ref(cur);
        g_signal_connect(P.clock, "after-paint",
                         G_CALLBACK(on_after_paint), NULL);
        P.last_us = 0;
    }
}

/* ---------------- 埋点 ---------------- */

void perf_phase_begin(PerfPhase phase)
{
    if (phase < 0 || phase >= PERF_PHASE_COUNT)
        return;

    P.start[phase] = g_get_monotonic_time();
}

void perf_phase_end(PerfPhase phase)
{
    double ms;
    gint64 t0;

    if (phase < 0 || phase >= PERF_PHASE_COUNT)
        return;

    t0 = P.start[phase];

    if (t0 == 0)
        return;

    P.start[phase] = 0;

    ms = (double)(g_get_monotonic_time() - t0) / 1000.0;

    P.ph[phase].sum_ms += ms;
    P.ph[phase].calls  += 1;
}

void perf_note_upload(gsize bytes)
{
    P.upload_bytes += bytes;
}

/* ---------------- 统计 ---------------- */

static void reset_stats(void)
{
    memset(P.ring, 0, sizeof(P.ring));
    P.ring_i    = 0;
    P.ring_n    = 0;
    P.spark_i   = 0;
    P.spark_n   = 0;
    P.last_us   = 0;
    P.win_start = 0;

    for (int i = 0; i < PERF_PHASE_COUNT; i++) {
        P.start[i]      = 0;
        P.ph[i].sum_ms  = 0.0;
        P.ph[i].calls   = 0;
        P.rate[i]       = 0.0;
        P.ms[i]         = 0.0;
    }

    P.upload_bytes = 0;
    P.upload_mbps  = 0.0;
    P.idle         = TRUE;
    P.fps          = 0.0;
    P.frame_ms     = 0.0;
    P.p95_ms       = 0.0;
    P.worst_ms     = 0.0;
    P.cpu_pct      = 0.0;
}

/* 每约 1 秒把「调用次数 / 总耗时」折成频率和平均耗时 */
static void rotate_window(gint64 now)
{
    double span;

    if (P.win_start == 0) {
        P.win_start = now;
        return;
    }

    span = (double)(now - P.win_start) / 1000000.0;

    if (span * 1000000.0 < PERF_ROTATE_US)
        return;

    P.cpu_pct = 0.0;

    for (int i = 0; i < PERF_PHASE_COUNT; i++) {
        PhaseStat *s = &P.ph[i];

        P.rate[i] = (double)s->calls / span;
        P.ms[i]   = s->calls > 0 ? s->sum_ms / (double)s->calls : 0.0;

        /* 每秒花掉的毫秒数 / 10 = 单核占用百分比 */
        P.cpu_pct += s->sum_ms / span / 10.0;

        s->calls  = 0;
        s->sum_ms = 0.0;
    }

    P.upload_mbps  = (double)P.upload_bytes / span / 1048576.0;
    P.upload_bytes = 0;
    P.win_start    = now;
}

static void refresh_frames(gint64 now)
{
    double  ms[PERF_RING];
    gint64  from = now - PERF_WINDOW_US;
    gint64  sum  = 0;
    guint   n    = 0;

    for (guint k = 0; k < P.ring_n; k++) {
        guint       slot = (P.ring_i + PERF_RING - P.ring_n + k) % PERF_RING;
        const Gap  *g    = &P.ring[slot];

        if (g->at_us < from || g->gap_us <= 0 || g->gap_us > PERF_IDLE_GAP)
            continue;

        ms[n++] = (double)g->gap_us / 1000.0;
        sum    += g->gap_us;
    }

    if (n < 8) {
        /* 最近 2 秒几乎没有持续重绘：画面是静止的，报「空闲」比报 0fps 清楚 */
        P.idle     = TRUE;
        P.fps      = 0.0;
        P.frame_ms = 0.0;
        P.p95_ms   = 0.0;
        P.worst_ms = 0.0;
        return;
    }

    P.idle = FALSE;
    P.fps  = (double)n * 1000000.0 / (double)sum;

    /* 帧间隔分布：插入排序，最多 240 个数 */
    for (guint i = 1; i < n; i++) {
        double v = ms[i];
        guint  j = i;

        while (j > 0 && ms[j - 1] > v) {
            ms[j] = ms[j - 1];
            j--;
        }

        ms[j] = v;
    }

    P.frame_ms = (double)((double)sum / (double)n) / 1000.0;
    P.p95_ms   = ms[(guint)((double)(n - 1) * 0.95)];
    P.worst_ms = ms[n - 1];
}

/* ---------------- 绘制 ---------------- */

static void draw_stat_row(cairo_t *cr, double x, double y, double w,
                          const char *name, double ms, double rate,
                          double refresh_ms)
{
    TextStyle label = TEXT_STYLE(FONT_MONO, 10.0, PANGO_WEIGHT_NORMAL, 0.0, col_dim);
    TextStyle value = TEXT_STYLE(FONT_MONO, 10.0, PANGO_WEIGHT_NORMAL, 0.0, col_text);
    TextStyle small = TEXT_STYLE(FONT_MONO, 9.0, PANGO_WEIGHT_NORMAL, 0.0, col_hint);
    g_autofree gchar *num = NULL;
    g_autofree gchar *freq = NULL;

    text_draw(cr, name, &label, x, y, TEXT_LEFT, 64.0);

    if (rate < 0.5) {
        text_draw(cr, "-", &small, x + 66.0, y, TEXT_LEFT, 0.0);

        /* 这一层这一秒没画：留一根空槽，读数位置不会左右跳 */
        color_set(cr, col_faint);
        path_round_rect(cr, x + w - 96.0, y + 5.0, 96.0, 4.0, 2.0);
        cairo_fill(cr);
        return;
    }

    num  = g_strdup_printf("%.2f ms", ms);
    freq = g_strdup_printf("%.0f/s", rate);

    text_draw(cr, num, &value, x + 66.0, y, TEXT_LEFT, 0.0);
    text_draw(cr, freq, &small, x + 122.0, y, TEXT_LEFT, 0.0);

    /* 调用频率相对显示器刷新率的小条：60/s 占满 */
    {
        double full = refresh_ms > 0.0 ? 1000.0 / refresh_ms : 60.0;
        double k    = full > 0.0 ? CLAMP(rate / full, 0.0, 1.0) : 0.0;

        color_set(cr, col_faint);
        path_round_rect(cr, x + w - 96.0, y + 5.0, 96.0, 4.0, 2.0);
        cairo_fill(cr);

        if (k > 0.0) {
            color_set(cr, RGBA(col_text.r, col_text.g, col_text.b, 0.55));
            path_round_rect(cr, x + w - 96.0, y + 5.0, 96.0 * k, 4.0, 2.0);
            cairo_fill(cr);
        }
    }
}

static void draw_spark(cairo_t *cr, double x, double y, double w, double h,
                       double refresh_ms)
{
    double scale_ms = MAX(33.4, refresh_ms * 2.2);
    double ty;
    guint  n = P.spark_n;

    color_set(cr, RGBA(1.0, 1.0, 1.0, 0.05));
    path_round_rect(cr, x, y, w, h, 4.0);
    cairo_fill(cr);

    /* 一条 60Hz 参考线：柱子超过它就是掉到下一档 */
    ty = y + h - CLAMP(refresh_ms / scale_ms, 0.0, 1.0) * h;
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x, ty + 0.5);
    cairo_line_to(cr, x + w, ty + 0.5);
    cairo_stroke(cr);

    if (n == 0)
        return;

    {
        double bw = w / (double)PERF_SPARK;

        for (guint i = 0; i < n; i++) {
            guint  slot = (P.spark_i + PERF_SPARK - n + i) % PERF_SPARK;
            double v    = P.spark[slot];
            double bh   = CLAMP(v / scale_ms, 0.0, 1.0) * (h - 2.0);
            Rgba   c    = v <= refresh_ms * 1.25 ? perf_ok
                        : v <= refresh_ms * 2.00 ? perf_warn : perf_bad;

            color_set(cr, c);
            cairo_rectangle(cr, x + (double)i * bw, y + h - bh,
                            MAX(bw - 1.0, 1.0), bh);
            cairo_fill(cr);
        }
    }
}

static const PerfPhase g_rows[PERF_ROWS][2] = {
    { PERF_PHASE_STAGE,        PERF_PHASE_COUNT },
    { PERF_PHASE_LYRIC,        PERF_PHASE_COUNT },
    { PERF_PHASE_WALL,         PERF_PHASE_COUNT },
    { PERF_PHASE_BACKDROP_GL,  PERF_PHASE_BACKDROP_CPU },
    { PERF_PHASE_CHROME,       PERF_PHASE_COUNT },
};

static const char *const g_row_name[PERF_ROWS] = {
    "stage", "lyric", "wall", "bg", "chrome"
};

static void draw_overlay(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data)
{
    TextStyle title = TEXT_STYLE(FONT_MONO, 9.5, PANGO_WEIGHT_NORMAL, 1.6, col_dim);
    TextStyle small = TEXT_STYLE(FONT_MONO, 9.5, PANGO_WEIGHT_NORMAL, 0.0, col_hint);
    TextStyle meta  = TEXT_STYLE(FONT_MONO, 9.5, PANGO_WEIGHT_NORMAL, 0.0, col_dim);
    TextStyle unit  = TEXT_STYLE(FONT_MONO, 10.0, PANGO_WEIGHT_NORMAL, 0.0, col_hint);
    double    pad  = PERF_PAD;
    double    x    = 18.0;
    double    y    = 18.0;
    double    w    = MIN(PERF_PANEL_W, (double)width - x * 2.0);
    double    in_x, in_w, yy;
    double    h, refresh_ms;
    g_autofree gchar *right = NULL;
    g_autofree gchar *line  = NULL;

    (void)area;
    (void)data;

    if (width <= 0 || height <= 0 || w < 220.0)
        return;

    refresh_ms = P.refresh_us > 0
               ? (double)P.refresh_us / 1000.0 : 1000.0 / 60.0;

    h = pad + 12.0 + 30.0 + 13.0 + 7.0 + 40.0 + 7.0
      + PERF_ROW_H * (double)PERF_ROWS + 7.0 + 13.0 + 13.0 + pad;

    if (h > (double)height - y * 2.0)
        h = (double)height - y * 2.0;

    in_x = x + pad;
    in_w = w - pad * 2.0;

    /* 底：半透明深色 + 一圈细边，压在画面左上角 */
    color_set(cr, RGBA(0.020, 0.020, 0.025, 0.88));
    path_round_rect(cr, x, y, w, h, 10.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* 面板文字进 uidbg 的 perf 层：MCP 的 ui_screen 能直接读到这些读数 */
    uidbg_begin_layer(UIDBG_LAYER_PERF);
    uidbg_layer_clear(UIDBG_LAYER_PERF);

    yy = y + pad;

    text_draw(cr, "性能检测 PERF", &title, in_x, yy, TEXT_LEFT, in_w - 60.0);
    right = g_strdup("F12");
    text_draw(cr, right, &small, in_x + in_w - text_width(cr, right, &small),
              yy, TEXT_LEFT, 0.0);

    /* 大字：帧率 */
    yy += 15.0;
    {
        TextStyle big = TEXT_STYLE(FONT_MONO, P.idle ? 19.0 : 27.0,
                                   PANGO_WEIGHT_BOLD, 0.0, col_text);
        g_autofree gchar *num = P.idle
            ? g_strdup("空闲")
            : g_strdup_printf("%.1f", P.fps);
        double nw = text_width(cr, num, &big);

        text_draw(cr, num, &big, in_x, yy, TEXT_LEFT, 0.0);

        if (!P.idle)
            text_draw(cr, "fps", &unit, in_x + nw + 6.0, yy + 11.0,
                      TEXT_LEFT, 0.0);

        /* 右侧：一帧多少毫秒 + 显示器刷新率 */
        {
            g_autofree gchar *frame = P.idle
                ? g_strdup("-")
                : g_strdup_printf("%.1f ms", P.frame_ms);
            g_autofree gchar *hz = g_strdup_printf("%.0f Hz", 1000.0 / refresh_ms);
            g_autofree gchar *both = g_strjoin("   ", frame, hz, NULL);

            text_draw(cr, both, &meta,
                      in_x + in_w - text_width(cr, both, &meta),
                      yy + 9.0, TEXT_LEFT, 0.0);
        }
    }

    yy += 30.0;

    if (P.idle) {
        line = g_strdup_printf("最近 2 秒没有持续重绘 · CPU %.1f%%", P.cpu_pct);
    } else {
        g_autofree gchar *avg  = g_strdup_printf("avg %.1f", P.frame_ms);
        g_autofree gchar *p95  = g_strdup_printf("p95 %.1f", P.p95_ms);
        g_autofree gchar *worst = g_strdup_printf("最慢 %.1f", P.worst_ms);
        g_autofree gchar *cpu  = g_strdup_printf("CPU %.1f%%", P.cpu_pct);

        line = g_strdup_printf("%s · %s · %s ms · %s", avg, p95, worst, cpu);
    }

    text_draw(cr, line, &meta, in_x, yy, TEXT_LEFT, in_w);
    yy += 13.0 + 7.0;

    draw_spark(cr, in_x, yy, in_w, 40.0, refresh_ms);
    yy += 40.0 + 7.0;

    for (int r = 0; r < PERF_ROWS; r++) {
        double ms = 0.0, rate = 0.0;

        for (int k = 0; k < 2; k++) {
            PerfPhase p = g_rows[r][k];

            if (p < 0 || p >= PERF_PHASE_COUNT)
                continue;

            ms   += P.ms[p] * P.rate[p];
            rate += P.rate[p];
        }

        if (rate > 0.0)
            ms /= rate;

        draw_stat_row(cr, in_x, yy, in_w, g_row_name[r], ms, rate, refresh_ms);
        yy += PERF_ROW_H;
    }

    yy += 7.0;

    line = g_strdup_printf("%d×%d · %d× · GL %s/%s/%s",
                           gtk_widget_get_width(app.window),
                           gtk_widget_get_height(app.window),
                           gtk_widget_get_scale_factor(app.window),
                           backdropgl_ready() ? "bg" : "--",
                           lyricgl_ready() ? "lyric" : "-----",
                           wallgl_ready() ? "wall" : "----");
    text_draw(cr, line, &small, in_x, yy, TEXT_LEFT, in_w);
    yy += 13.0;

    {
        g_autofree gchar *frames = g_strdup_printf("帧 %" G_GINT64_FORMAT,
                                                   P.frames_total);
        g_autofree gchar *upload = g_strdup_printf("上传 %.1f MB/s", P.upload_mbps);
        g_autofree gchar *both   = g_strjoin(" · ", frames, upload, NULL);

        text_draw(cr, both, &small, in_x, yy, TEXT_LEFT, in_w);
    }

    uidbg_begin_layer(UIDBG_LAYER_STAGE);
}

/* ---------------- 生命周期 ---------------- */

static gboolean on_update(gpointer data)
{
    gint64 now = g_get_monotonic_time();

    (void)data;

    bind_clock();
    rotate_window(now);
    refresh_frames(now);

    if (P.area != NULL)
        gtk_widget_queue_draw(P.area);

    return G_SOURCE_CONTINUE;
}

GtkWidget *perf_new(GtkWidget *window)
{
    GtkWidget *area = gtk_drawing_area_new();

    P.window = window;
    P.area   = area;

    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_widget_set_can_target(area, FALSE);
    gtk_widget_add_css_class(area, "perf-overlay");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_overlay,
                                   NULL, NULL);
    gtk_widget_set_visible(area, FALSE);

    reset_stats();
    bind_clock();

    if (g_getenv("FOLLOW_PERF") != NULL &&
        g_strcmp0(g_getenv("FOLLOW_PERF"), "0") != 0)
        perf_toggle();

    return area;
}

void perf_clear(void)
{
    if (P.timer != 0) {
        g_source_remove(P.timer);
        P.timer = 0;
    }

    if (P.clock != NULL) {
        g_signal_handlers_disconnect_by_func(P.clock,
                                             G_CALLBACK(on_after_paint), NULL);
        g_clear_object(&P.clock);
    }

    P.area   = NULL;
    P.window = NULL;
    P.on     = FALSE;
}

void perf_toggle(void)
{
    if (P.area == NULL)
        return;

    P.on = !P.on;
    gtk_widget_set_visible(P.area, P.on);

    if (P.on) {
        bind_clock();
        reset_stats();

        if (P.timer == 0)
            P.timer = g_timeout_add(PERF_UPDATE_MS, on_update, NULL);

        /* 立刻出第一帧读数，不用等 200ms */
        on_update(NULL);
    } else if (P.timer != 0) {
        g_source_remove(P.timer);
        P.timer = 0;

        /* 面板已经不画了：把 uidbg 里最后一帧的读数抹掉，别让
         * ui_screen 读到已经不在画面上的文字 */
        uidbg_layer_clear(UIDBG_LAYER_PERF);
    }
}

gboolean perf_visible(void)
{
    return P.on;
}
