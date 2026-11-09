#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <math.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pango/pangocairo.h>

#include "cache.h"
#include "config.h"
#include "http.h"
#include "lyrics.h"
#include "lyricsrc.h"
#include "media.h"
#include "palette.h"
#include "platform.h"
#include "qrcode.h"
#include "source.h"
#include "stats.h"
#include "texture.h"

/* 布局参考 Sowon2（Tsoding 的 Jai + raylib 播放器）：近黑底、左上超大时钟、
 * 右上是封面 / ♪ 曲名 ♪ / 单行歌词，下面是二维码 + 胶囊，纯键盘操作。
 *
 * 绘制是"即时模式"：GTK 只提供窗口 / 键盘 / 音频，整屏由 render() 用 cairo 画。
 * 画面没有逐帧动画，只在「秒数 / 歌词行 / 状态 / 尺寸」变化时重画。
 *
 * 二维码规则：
 *   本地文件的曲目   → 不显示曲目二维码
 *   网络链接的曲目   → 显示曲目二维码（下面是平台徽标）
 *   [qrcode] link   → 是网络地址就再显示一个"自定义链接"二维码（下面显示地址胶囊）
 *
 *   ┌────────────────────────────────────────────────┐
 *   │ 00:12                          ┌──────────┐    │
 *   │                                │  cover   │    │
 *   │                                └──────────┘    │
 *   │ ────────  04:01              ♪ 李荣浩 - 耳朵 ♪  │
 *   │ F1 SHORTCUTS                    只因你太美      │
 *   │                               ▛▀▜    ▛▀▜       │
 *   │                            [BILIBILI][github…]  │
 *   └────────────────────────────────────────────────┘
 */

#define CSS_PATH    "src/style/style.css"
#define CONFIG_PATH "src/config.ini"

#define TICK_MS     100

/* 字体栈：时间用带手写感的，其余等宽（Pango 不按列表逐字回退，等宽用通用族名） */
#define FONT_HAND "Segoe Print, Caveat, Comic Sans MS, Microsoft YaHei UI, cursive"
#define FONT_MONO "monospace"

#define COL_BG     0.039, 0.039, 0.039
#define COL_TEXT   0.902, 0.902, 0.910, 1.00
#define COL_DIM    0.902, 0.902, 0.910, 0.45
#define COL_HINT   0.902, 0.902, 0.910, 0.55
#define COL_FAINT  0.902, 0.902, 0.910, 0.14

static Media media;

/* 一个二维码槽：内容 + 胶囊文字 + （可选）平台图标 + 缓存 */
typedef struct {
    gchar           *text;
    gchar           *label;
    Platform         platform;
    gboolean         with_icon;
    QRcode          *code;
    cairo_surface_t *surface;
    int              pixels;    /* 缓存 surface 的边长（两个二维码强制同一尺寸） */
} QrSlot;

static struct {
    GtkWidget  *window;
    GtkWidget  *canvas;

    guint       tick_id;
    gint        track;
    gboolean    help;
    gboolean    dirty;
    gboolean    last_playing;
    gboolean    last_paused;
    gint        last_width;
    gint        last_height;
    gint        shown_second;

    gchar      *lyric_line;
    gchar      *lyric_error;    /* 歌词找不到 / 解析失败的原因，显示在字幕行 */
    Lyrics     *lyrics;

    QrSlot      qr[2];          /* 0 = 曲目页面，1 = 自定义链接 */

    gchar      *resolved_title;
    gchar      *resolved_artist;

    cairo_surface_t *cover;
    cairo_surface_t *cover_scaled;
    int              cover_scaled_px;
    guint            cover_gen;      /* 丢弃过期的网络封面下载 */

    Palette          palette;        /* 封面里取的种子色 */

    /* 动态背景：一张很小（≈320px 宽）的纹理交给 GtkPicture，
     * 由 GTK 的 GL 渲染器负责放大/合成 —— CPU 每帧只生成这么点像素。 */
    GtkWidget       *bg_picture;
    cairo_surface_t *bg_base;        /* 模糊封面缩略图（换封面/改宽高比时重建） */
    cairo_surface_t *bg_frame;       /* 每帧复用的画布 */
    int              bg_base_w, bg_base_h;
    double           bg_phase;       /* 动画相位（秒） */
    guint            bg_tick_id;

    /* 收听统计（左下角热力图） */
    Stats        stats;
    gint64       stats_last_us;      /* 上一次 tick 的时间 */
    gint64       stats_saved_us;     /* 上次落盘时间 */

    gint         lrc_offset_ms;      /* 歌词偏移（运行时可用 [ ] 微调） */
    gint         lrc_offset_base;    /* ini 里的初始值，\ 键重置用 */
    gboolean     lyrics_pending;     /* 正在从在线源取歌词 */
    gboolean     lyrics_fit_checked; /* 时长诊断只做一次 */
    gchar       *lyrics_spec;        /* 当前歌词源描述（日志用） */
    guint        lyrics_gen;         /* 丢弃过期的取歌词结果 */

    gint             repeat;         /* RepeatMode */
    gboolean         shuffle;
    gint             history[64];    /* 随机播放的历史，用来支持"上一首" */
    gint             history_len;

    guint       resolve_gen;
} ui;

/* 一次绘制用的几何量：所有位置/尺寸都留在这里算，render() 只管照单作画 */
typedef struct {
    double pad_x, pad_y;

    double note_px, note_y, note_h;   /* 大时间上方的一行备注 */
    double note_max_w;
    double clock_px, clock_y, bar_x, bar_y, bar_w, bar_h, dur_x, hint_y;

    double col_cx, col_w;
    double cover_y, cover_px;
    double now_y, now_px;
    double lyric_y, lyric_px;
    double qr_y, qr_px, cell_w, cell_gap;
    double badge_y, badge_h, badge_gap;
    double text_max;

    /* 左下角：墙钟 / 收听时长 / 热力图 */
    double wall_clock_px, stats_y, stat_px;
    double heat_cell, heat_gap, heat_w, heat_h;

    int    n_qr;
} Layout;

static gchar *current_label(void);
static void   history_push(gint index);
static void   load_lyrics(const AudioTrack *track);
static void   drop_background(void);

/* ---------- 小工具 ---------- */

static gchar *format_time(double seconds)
{
    int total;

    if (seconds < 0.0)
        seconds = 0.0;

    total = (int)(seconds + 0.5);

    if (total >= 3600)
        return g_strdup_printf("%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);

    return g_strdup_printf("%02d:%02d", total / 60, total % 60);
}

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void request_redraw(void)
{
    ui.dirty = TRUE;
}

static PangoLayout *make_layout(cairo_t *cr, const char *text, const char *family,
                                double px, PangoWeight weight, double letter_spacing)
{
    PangoLayout          *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_new();

    pango_font_description_set_family(desc, family);
    pango_font_description_set_absolute_size(desc, px * PANGO_SCALE);
    pango_font_description_set_weight(desc, weight);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    if (letter_spacing > 0.1) {
        PangoAttrList *attrs = pango_attr_list_new();

        pango_attr_list_insert(attrs,
                               pango_attr_letter_spacing_new((int)(letter_spacing * PANGO_SCALE)));
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }

    pango_layout_set_text(layout, text, -1);
    return layout;
}

static void draw_text(cairo_t *cr, const char *text, const char *family, double px,
                      PangoWeight weight, double letter_spacing,
                      double r, double g, double b, double a,
                      double x, double y, gboolean center)
{
    PangoLayout *layout;
    int          tw = 0, th = 0;

    if (text == NULL || *text == '\0')
        return;

    layout = make_layout(cr, text, family, px, weight, letter_spacing);
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (center)
        x -= tw * 0.5;

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

/* 居中 + 限宽 + 省略号 */
static void draw_text_ellipsized(cairo_t *cr, const char *text, const char *family, double px,
                                 PangoWeight weight, double max_width,
                                 double r, double g, double b, double a,
                                 double cx, double y)
{
    PangoLayout *layout;

    if (text == NULL || *text == '\0' || max_width <= 0.0)
        return;

    layout = make_layout(cr, text, family, px, weight, 0.0);
    pango_layout_set_width(layout, (int)(max_width * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, cx - max_width * 0.5, y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

/* 左对齐 + 限宽 + 省略号（备注这类长文本用） */
static void draw_text_left(cairo_t *cr, const char *text, const char *family, double px,
                           PangoWeight weight, double letter_spacing, double max_width,
                           double r, double g, double b, double a, double x, double y)
{
    PangoLayout *layout;
    int          th = 0;

    if (text == NULL || *text == '\0' || max_width <= 8.0)
        return;

    layout = make_layout(cr, text, family, px, weight, letter_spacing);

    pango_layout_set_width(layout, (int)(max_width * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_get_pixel_size(layout, NULL, &th);

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

static int measure_text(cairo_t *cr, const char *text, const char *family,
                        double px, PangoWeight weight)
{
    PangoLayout *layout;
    int          tw = 0;

    if (text == NULL || *text == '\0')
        return 0;

    layout = make_layout(cr, text, family, px, weight, 0.0);
    pango_layout_get_pixel_size(layout, &tw, NULL);
    g_object_unref(layout);

    return tw;
}

static int measure_text_h(cairo_t *cr, const char *text, const char *family,
                          double px, PangoWeight weight)
{
    PangoLayout *layout;
    int          th = 0;

    if (text == NULL || *text == '\0')
        return 0;

    layout = make_layout(cr, text, family, px, weight, 0.0);
    pango_layout_get_pixel_size(layout, NULL, &th);
    g_object_unref(layout);

    return th;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    double r = clampd(radius, 0.0, MIN(w, h) * 0.5);

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

/* "https://www.github.com/x/y" → "github.com"（胶囊上只显示主机名，短且好认） */
static gchar *url_host(const char *url)
{
    const char *p = url;
    const char *end;

    if (url == NULL)
        return NULL;

    if (g_str_has_prefix(p, "https://")) p += 8;
    else if (g_str_has_prefix(p, "http://")) p += 7;

    if (g_str_has_prefix(p, "www.")) p += 4;

    end = p;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#')
        end++;

    return g_strndup(p, (gsize)(end - p));
}

/* "https://www.github.com/clevebitr/" → "github.com/clevebitr" */
static gchar *short_url(const char *url)
{
    const char *p = url;
    gchar      *out;

    if (url == NULL)
        return NULL;

    if (g_str_has_prefix(p, "https://")) p += 8;
    else if (g_str_has_prefix(p, "http://")) p += 7;

    if (g_str_has_prefix(p, "www.")) p += 4;

    out = g_strdup(p);

    {
        gsize len = strlen(out);

        while (len > 0 && out[len - 1] == '/')
            out[--len] = '\0';
    }

    return out;
}

/* ---------- 封面 ---------- */

/* B 站图床的 @... 后缀改成正方形 JPEG；其它地址的 .avif 换 .jpg
 * —— 这套 GDK 没有 AVIF 解码器，直接吃会失败。 */
static gchar *cover_url_normalize(const char *url)
{
    if (url == NULL)
        return NULL;

    if (strstr(url, "hdslb.com") != NULL) {
        const char *at = strchr(url, '@');

        if (at != NULL) {
            g_autofree gchar *base = g_strndup(url, (gsize)(at - url));

            return g_strdup_printf("%s@672w_672h_1c.jpg", base);
        }
    }

    if (g_str_has_suffix(url, ".avif")) {
        g_autofree gchar *base = g_strndup(url, strlen(url) - 5);

        return g_strconcat(base, ".jpg", NULL);
    }

    return g_strdup(url);
}

/* 把 gdk-pixbuf 装成当前封面：取种子色 + 降到 768px 的 cairo surface。
 * gdk-pixbuf 的像素是非预乘、通道序 R,G,B[,A]，比 gdk_texture_download 可靠得多。 */
static void install_cover_pixbuf(GdkPixbuf *pixbuf)
{
    int           w, h, nch, stride;
    guchar       *rgba;
    const guchar *px;

    if (pixbuf == NULL)
        return;

    w      = gdk_pixbuf_get_width(pixbuf);
    h      = gdk_pixbuf_get_height(pixbuf);
    nch    = gdk_pixbuf_get_n_channels(pixbuf);
    stride = gdk_pixbuf_get_rowstride(pixbuf);
    px     = gdk_pixbuf_get_pixels(pixbuf);

    if (w <= 0 || h <= 0 || nch < 3)
        return;

    rgba = g_malloc_n((gsize)w * 4, (gsize)h);

    for (int y = 0; y < h; y++) {
        const guchar *s = px + (gsize)y * (gsize)stride;
        guchar       *d = rgba + (gsize)y * (gsize)w * 4;

        for (int x = 0; x < w; x++) {
            d[x * 4 + 0] = s[x * nch + 0];
            d[x * 4 + 1] = s[x * nch + 1];
            d[x * 4 + 2] = s[x * nch + 2];
            d[x * 4 + 3] = nch >= 4 ? s[x * nch + 3] : 255;
        }
    }

    palette_from_rgba(rgba, w, h, &ui.palette);

    if (ui.cover != NULL) {
        cairo_surface_destroy(ui.cover);
        ui.cover = NULL;
    }

    ui.cover = rgba_to_cairo_surface_fit(rgba, w, h, 768);

    if (ui.cover_scaled != NULL) {
        cairo_surface_destroy(ui.cover_scaled);
        ui.cover_scaled = NULL;
        ui.cover_scaled_px = 0;
    }

    g_free(rgba);

    drop_background();
}

typedef struct {
    guint    gen;
    gchar   *url;
    gchar   *cache_path;   /* 下载成功后写到这里 */
    GBytes  *bytes;
    gchar   *error;
} CoverJob;

static gboolean cover_done(gpointer data)
{
    CoverJob *job = data;

    if (job->gen == ui.cover_gen) {
        if (job->bytes != NULL) {
            g_autoptr(GError) error = NULL;
            GInputStream     *stream = g_memory_input_stream_new_from_bytes(job->bytes);
            GdkPixbuf        *pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);

            if (pixbuf != NULL) {
                install_cover_pixbuf(pixbuf);
                g_object_unref(pixbuf);
                request_redraw();

                /* 顺手落盘，下次直接读 */
                if (job->cache_path != NULL && job->bytes != NULL) {
                    gsize   size = 0;
                    gconstpointer data = g_bytes_get_data(job->bytes, &size);

                    g_file_set_contents(job->cache_path, data, (gssize)size, NULL);
                }
            } else {
                g_warning("cover: cannot decode %s: %s", job->url,
                          error != NULL ? error->message : "unknown error");
            }

            g_object_unref(stream);
        } else {
            g_warning("cover: download failed %s: %s", job->url,
                      job->error != NULL ? job->error : "unknown error");
        }
    }

    if (job->bytes != NULL)
        g_bytes_unref(job->bytes);

    g_free(job->url);
    g_free(job->cache_path);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer cover_thread(gpointer data)
{
    CoverJob     *job = data;
    HttpResponse  resp = {0};

    if (http_get(job->url, &resp, 20) == 0 && resp.status == 200)
        job->bytes = g_bytes_new(resp.body, resp.length);
    else if (resp.error[0] != 0)
        job->error = g_strdup(resp.error);
    else
        job->error = g_strdup_printf("HTTP %ld", resp.status);

    http_response_free(&resp);

    g_idle_add(cover_done, job);
    return NULL;
}

static void set_cover(const AudioTrack *track)
{
    g_autofree gchar *path = NULL;
    g_autoptr(GError) error = NULL;
    GdkPixbuf        *pixbuf = NULL;

    if (ui.cover != NULL) {
        cairo_surface_destroy(ui.cover);
        ui.cover = NULL;
    }

    if (ui.cover_scaled != NULL) {
        cairo_surface_destroy(ui.cover_scaled);
        ui.cover_scaled = NULL;
        ui.cover_scaled_px = 0;
    }

    ui.cover_gen++;          /* 作废还在下载的旧封面 */
    palette_reset(&ui.palette);
    drop_background();
    request_redraw();

    if (track == NULL || track->pic == NULL || track->pic[0] == 0)
        return;

    if (track->pic_remote) {
        g_autofree gchar *cache_path = follow_cache_file("cover", track->pic, "img");

        /* 命中缓存就直接读，不再走网络 */
        if (cache_path != NULL && g_file_test(cache_path, G_FILE_TEST_EXISTS)) {
            GdkPixbuf *cached = gdk_pixbuf_new_from_file(cache_path, NULL);

            if (cached != NULL) {
                install_cover_pixbuf(cached);
                g_object_unref(cached);
                g_message("cover: cache hit %s", cache_path);
                return;
            }
        }

        {
            CoverJob *job = g_new0(CoverJob, 1);

            job->gen        = ui.cover_gen;
            job->url        = cover_url_normalize(track->pic);
            job->cache_path = g_steal_pointer(&cache_path);

            g_message("cover: fetching %s", job->url);
            g_thread_unref(g_thread_new("follow-cover", cover_thread, job));
            return;
        }
    }

    path = config_to_local_path(track->pic);
    if (path == NULL)
        return;

    pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if (pixbuf == NULL) {
        g_warning("cover: cannot load %s: %s", path,
                  error != NULL ? error->message : "unknown error");
        return;
    }

    install_cover_pixbuf(pixbuf);
    g_object_unref(pixbuf);
}

/* 逐级减半缩小的模糊：cairo 的双线性放大把 28px 的小图摊开就是柔和模糊，
 * 比手写卷积快得多（iTunes 那个 blur 也是这么做的）。 */
static cairo_surface_t *blur_surface(cairo_surface_t *src, int max_dim)
{
    cairo_surface_t *cur;
    int              w, h;

    if (src == NULL)
        return NULL;

    cur = cairo_surface_reference(src);
    w   = cairo_image_surface_get_width(cur);
    h   = cairo_image_surface_get_height(cur);

    while (w > max_dim || h > max_dim) {
        int              nw = MAX(1, w / 2);
        int              nh = MAX(1, h / 2);
        cairo_surface_t *next = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw, nh);
        cairo_t         *cr;

        if (cairo_surface_status(next) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(next);
            break;
        }

        cr = cairo_create(next);
        cairo_scale(cr, (double)nw / (double)w, (double)nh / (double)h);
        cairo_set_source_surface(cr, cur, 0.0, 0.0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_destroy(cr);

        cairo_surface_destroy(cur);
        cur = next;
        w   = nw;
        h   = nh;
    }

    return cur;
}

static void drop_background(void)
{
    if (ui.bg_base != NULL) {
        cairo_surface_destroy(ui.bg_base);
        ui.bg_base = NULL;
    }

    if (ui.bg_frame != NULL) {
        cairo_surface_destroy(ui.bg_frame);
        ui.bg_frame = NULL;
    }

    ui.bg_base_w = 0;
    ui.bg_base_h = 0;
}

/* 小纹理的尺寸：宽度固定，高度跟着窗口宽高比走（避免拉伸变形） */
static void bg_small_size(int width, int height, int *out_w, int *out_h)
{
    int w = 256;

    if (width > 0 && height > 0) {
        int h = (int)((double)w * (double)height / (double)width + 0.5);

        if (h < 96) h = 96;
        if (h > 640) h = 640;

        *out_w = w;
        *out_h = h;
    } else {
        *out_w = w;
        *out_h = 180;
    }
}

/* 模糊封面缩略图：只在换封面 / 宽高比变化时重建 */
static void build_bg_base(int width, int height)
{
    int      sw, sh;
    cairo_t *cr;

    bg_small_size(width, height, &sw, &sh);

    if (ui.bg_base != NULL && ui.bg_base_w == sw && ui.bg_base_h == sh)
        return;

    if (ui.bg_base != NULL)
        cairo_surface_destroy(ui.bg_base);

    ui.bg_base    = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    ui.bg_base_w  = sw;
    ui.bg_base_h  = sh;

    if (cairo_surface_status(ui.bg_base) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(ui.bg_base);
        ui.bg_base = NULL;
        return;
    }

    cr = cairo_create(ui.bg_base);

    cairo_set_source_rgba(cr, 0.035, 0.035, 0.040, 1.0);
    cairo_paint(cr);

    if (ui.cover != NULL) {
        cairo_surface_t *small = blur_surface(ui.cover, 160);

        if (small != NULL) {
            double iw = (double)cairo_image_surface_get_width(small);
            double ih = (double)cairo_image_surface_get_height(small);

            if (iw > 0.0 && ih > 0.0) {
                double scale = MAX((double)sw / iw, (double)sh / ih) * 1.12;

                surface_box_blur(small, MAX(2, (int)(iw / 18.0)), 3);

                cairo_save(cr);
                cairo_translate(cr, ((double)sw - iw * scale) * 0.5,
                                ((double)sh - ih * scale) * 0.5);
                cairo_scale(cr, scale, scale);
                cairo_set_source_surface(cr, small, 0.0, 0.0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
                cairo_paint(cr);
                cairo_restore(cr);
            }

            cairo_surface_destroy(small);
        }
    }

    cairo_destroy(cr);
}

/* cairo 的 ARGB32（小端 = BGRA，预乘）正好等于 GDK_MEMORY_B8G8R8A8_PREMULTIPLIED */
static GdkTexture *texture_from_surface(cairo_surface_t *surface)
{
    int         w, h, stride;
    guchar     *data;
    GBytes     *bytes;
    GdkTexture *texture;

    if (surface == NULL)
        return NULL;

    w      = cairo_image_surface_get_width(surface);
    h      = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);
    data   = cairo_image_surface_get_data(surface);

    if (w <= 0 || h <= 0 || data == NULL)
        return NULL;

    cairo_surface_flush(surface);

    bytes   = g_bytes_new(data, (gsize)stride * (gsize)h);
    texture = gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED,
                                     bytes, (gsize)stride);
    g_bytes_unref(bytes);

    return texture;
}

/* 每帧：模糊封面 + 三团缓慢漂移的种子色渐变 + 压暗，画进小纹理。
 * 320x180 的像素量，CPU 上不到 1ms，放大交给 GPU。 */
/* RGB → HSV 平移色相 → RGB，让渐变颜色随时间缓慢呼吸 */
static void hue_shift(double *r, double *g, double *b, double degrees)
{
    double max = MAX(*r, MAX(*g, *b));
    double min = MIN(*r, MIN(*g, *b));
    double d = max - min;
    double h, s, v, c, x, m;

    if (d <= 1e-6)
        return;

    v = max;
    s = d / max;

    if (max == *r)      h = fmod((*g - *b) / d, 6.0);
    else if (max == *g) h = (*b - *r) / d + 2.0;
    else                h = (*r - *g) / d + 4.0;

    h = fmod(h + degrees / 60.0, 6.0);
    if (h < 0.0)
        h += 6.0;

    c = v * s;
    x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
    m = v - c;

    if (h < 1.0)      { *r = c; *g = x; *b = 0; }
    else if (h < 2.0) { *r = x; *g = c; *b = 0; }
    else if (h < 3.0) { *r = 0; *g = c; *b = x; }
    else if (h < 4.0) { *r = 0; *g = x; *b = c; }
    else if (h < 5.0) { *r = x; *g = 0; *b = c; }
    else              { *r = c; *g = 0; *b = x; }

    *r += m;
    *g += m;
    *b += m;
}

static void render_bg_frame(double phase)
{
    int      sw, sh;
    cairo_t *cr;

    if (ui.bg_frame == NULL)
        return;

    sw = cairo_image_surface_get_width(ui.bg_frame);
    sh = cairo_image_surface_get_height(ui.bg_frame);

    cr = cairo_create(ui.bg_frame);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

    if (ui.bg_base != NULL) {
        cairo_set_source_surface(cr, ui.bg_base, 0.0, 0.0);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgba(cr, 0.035, 0.035, 0.040, 1.0);
        cairo_paint(cr);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* 三团渐变：位置/半径按不同频率缓慢漂移，形成 Apple 那种"流动"感 */
    {
        struct { double bx, by, speed, phase, radius, alpha, r, g, b; } blobs[3] = {
            { 0.76, 0.18, 0.50, 0.0, 1.15, 0.58,
              ui.palette.seed_r, ui.palette.seed_g, ui.palette.seed_b },
            { 0.14, 0.68, 0.37, 2.1, 1.00, 0.46,
              ui.palette.alt_r,  ui.palette.alt_g,  ui.palette.alt_b  },
            { 0.55, 1.00, 0.29, 4.2, 0.95, 0.44,
              ui.palette.deep_r, ui.palette.deep_g, ui.palette.deep_b },
        };

        for (int i = 0; i < 3; i++) {
            double           a = phase * blobs[i].speed + blobs[i].phase;
            double           x = (blobs[i].bx + 0.30 * sin(a)) * (double)sw;
            double           y = (blobs[i].by + 0.24 * cos(a * 0.83)) * (double)sh;
            double           r = blobs[i].radius * (double)MAX(sw, sh)
                                 * (1.0 + 0.22 * sin(a * 0.7));
            double           alpha = blobs[i].alpha * (1.0 + 0.28 * sin(a * 0.55 + 1.0));
            double           br = blobs[i].r, bg = blobs[i].g, bb = blobs[i].b;
            cairo_pattern_t *p;

            hue_shift(&br, &bg, &bb, 14.0 * sin(a * 0.45 + (double)i));

            p = cairo_pattern_create_radial(x, y, 0.0, x, y, r);

            cairo_pattern_add_color_stop_rgba(p, 0.00, br, bg, bb, alpha);
            cairo_pattern_add_color_stop_rgba(p, 0.45, br, bg, bb, alpha * 0.42);
            cairo_pattern_add_color_stop_rgba(p, 1.00, br, bg, bb, 0.0);

            cairo_set_source(cr, p);
            cairo_paint(cr);
            cairo_pattern_destroy(p);
        }
    }

    /* 压暗，保证白字 / 二维码可读 */
    {
        cairo_pattern_t *scrim = cairo_pattern_create_linear(0.0, 0.0, 0.0, (double)sh);

        cairo_pattern_add_color_stop_rgba(scrim, 0.0, 0.0, 0.0, 0.0, 0.34);
        cairo_pattern_add_color_stop_rgba(scrim, 0.55, 0.0, 0.0, 0.0, 0.56);
        cairo_pattern_add_color_stop_rgba(scrim, 1.0, 0.0, 0.0, 0.0, 0.80);

        cairo_set_source(cr, scrim);
        cairo_paint(cr);
        cairo_pattern_destroy(scrim);
    }

    cairo_destroy(cr);
}

static void push_bg_frame(int width, int height)
{
    int sw, sh;

    bg_small_size(width, height, &sw, &sh);

    if (ui.bg_frame == NULL ||
        cairo_image_surface_get_width(ui.bg_frame) != sw ||
        cairo_image_surface_get_height(ui.bg_frame) != sh) {
        if (ui.bg_frame != NULL)
            cairo_surface_destroy(ui.bg_frame);

        ui.bg_frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);

        if (cairo_surface_status(ui.bg_frame) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(ui.bg_frame);
            ui.bg_frame = NULL;
            return;
        }
    }

    build_bg_base(width, height);
    render_bg_frame(ui.bg_phase);

    if (ui.bg_picture != NULL) {
        GdkTexture *texture = texture_from_surface(ui.bg_frame);

        if (texture != NULL) {
            gtk_picture_set_paintable(GTK_PICTURE(ui.bg_picture), GDK_PAINTABLE(texture));
            g_object_unref(texture);
        }
    }
}

static gboolean on_bg_tick(gpointer data)
{
    int width  = gtk_widget_get_width(ui.window);
    int height = gtk_widget_get_height(ui.window);

    (void)data;

    if (width <= 0 || height <= 0 || !gtk_widget_get_mapped(ui.window))
        return G_SOURCE_CONTINUE;

    ui.bg_phase += 0.0333;        /* ~30fps */
    push_bg_frame(width, height);

    return G_SOURCE_CONTINUE;
}

static cairo_surface_t *cover_for_size(double size)
{
    double   sw, sh, scale;
    int      target, dw, dh;
    cairo_t *cr;

    if (ui.cover == NULL)
        return NULL;

    target = ((int)size / 4) * 4;
    if (target < 4)
        target = 4;

    if (ui.cover_scaled != NULL && ui.cover_scaled_px == target)
        return ui.cover_scaled;

    if (ui.cover_scaled != NULL) {
        cairo_surface_destroy(ui.cover_scaled);
        ui.cover_scaled = NULL;
    }

    sw = (double)cairo_image_surface_get_width(ui.cover);
    sh = (double)cairo_image_surface_get_height(ui.cover);
    if (sw <= 0.0 || sh <= 0.0)
        return NULL;

    scale = (double)target / MAX(sw, sh);
    dw = MAX(1, (int)(sw * scale + 0.5));
    dh = MAX(1, (int)(sh * scale + 0.5));

    ui.cover_scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    ui.cover_scaled_px = target;

    if (cairo_surface_status(ui.cover_scaled) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(ui.cover_scaled);
        ui.cover_scaled = NULL;
        return NULL;
    }

    cr = cairo_create(ui.cover_scaled);
    cairo_scale(cr, (double)dw / sw, (double)dh / sh);
    cairo_set_source_surface(cr, ui.cover, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_destroy(cr);

    return ui.cover_scaled;
}

static void draw_cover(cairo_t *cr, double x, double y, double size)
{
    cairo_surface_t *scaled;
    double           tw, th, dx, dy;

    if (size <= 1.0)
        return;

    if (ui.cover == NULL) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.04);
        cairo_rectangle(cr, x, y, size, size);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x + 0.5, y + 0.5, size - 1.0, size - 1.0);
        cairo_stroke(cr);
        return;
    }

    scaled = cover_for_size(size);
    if (scaled == NULL)
        return;

    tw = (double)cairo_image_surface_get_width(scaled);
    th = (double)cairo_image_surface_get_height(scaled);
    if (tw <= 0.0 || th <= 0.0)
        return;

    dx = floor(x + (size - tw) * 0.5);
    dy = floor(y + (size - th) * 0.5);

    cairo_set_source_surface(cr, scaled, dx, dy);
    cairo_paint(cr);
}

/* ---------- 二维码槽 ---------- */

static void qr_slot_clear(QrSlot *slot)
{
    /* 注意：free 完必须置 NULL —— qr_slot_set() 也会先 clear 一次，
     * 否则会第二次 free 同一个指针（切歌时 double free 直接崩） */
    g_clear_pointer(&slot->text, g_free);
    g_clear_pointer(&slot->label, g_free);

    if (slot->code != NULL) {
        QRcode_free(slot->code);
        slot->code = NULL;
    }

    if (slot->surface != NULL) {
        cairo_surface_destroy(slot->surface);
        slot->surface = NULL;
    }

    slot->pixels   = 0;
    slot->platform = PLATFORM_UNKNOWN;
    slot->with_icon = FALSE;
}

static void qr_slot_set(QrSlot *slot, const char *text, const char *label,
                        Platform platform, gboolean with_icon)
{
    qr_slot_clear(slot);

    if (text == NULL || *text == '\0')
        return;

    slot->text      = g_strdup(text);
    slot->label     = label != NULL ? g_strdup(label) : short_url(text);
    slot->platform  = platform;
    slot->with_icon = with_icon;
    slot->code      = qrcode_generate(text);

    if (slot->code == NULL)
        g_warning("Qrcode generate failed: %s", text);
}

/* 预渲染成边长为 size 的正方形：模块大小是分数，但每条边都对齐到像素，
 * 所以不同版本（模块数不同）的二维码画出来尺寸完全一致。 */
static cairo_surface_t *qr_slot_surface(QrSlot *slot, double size)
{
    int      n, side;
    double   m;
    cairo_t *cr;

    if (slot->code == NULL)
        return NULL;

    n = slot->code->width;
    if (n <= 0)
        return NULL;

    side = MAX(8, (int)floor(size + 0.5));

    if (slot->surface != NULL && slot->pixels == side)
        return slot->surface;

    if (slot->surface != NULL) {
        cairo_surface_destroy(slot->surface);
        slot->surface = NULL;
    }

    slot->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
    slot->pixels  = side;

    if (cairo_surface_status(slot->surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(slot->surface);
        slot->surface = NULL;
        return NULL;
    }

    cr = cairo_create(slot->surface);

    cairo_set_source_rgba(cr, COL_BG, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);

    m = (double)side / (double)(n + 2);   /* 含四周一个模块静区 */

    for (int my = 0; my < n; my++) {
        for (int mx = 0; mx < n; mx++) {
            int x0, x1, y0, y1;

            if (!(slot->code->data[my * n + mx] & 1))
                continue;

            x0 = (int)lround((double)(mx + 1) * m);
            x1 = (int)lround((double)(mx + 2) * m);
            y0 = (int)lround((double)(my + 1) * m);
            y1 = (int)lround((double)(my + 2) * m);

            cairo_rectangle(cr, x0, y0,
                            MAX(1, x1 - x0), MAX(1, y1 - y0));
        }
    }

    cairo_fill(cr);
    cairo_destroy(cr);

    return slot->surface;
}

/* 胶囊：图标（可选）+ 文字。宽度跟着文字走，最长不超过 max_w，超出就省略。
 * 文字垂直居中、水平留白都用 padding 算，避免"大小不太对"。 */
static void draw_capsule(cairo_t *cr, const char *text, Platform platform, gboolean with_icon,
                         double cx, double y, double h, double max_w)
{
    PangoLayout *layout;
    int          tw = 0, th = 0;
    double       pad, icon, gap, font_px, hh, cy, w, x, k;
    double       natural, want, allowed;
    double       r = 0.902, g = 0.902, b = 0.910;

    if (text == NULL || *text == '\0' || h < 6.0 || max_w < 24.0)
        return;

    font_px = h * 0.46;
    pad     = h * 0.42;
    icon    = with_icon ? h * 0.62 : 0.0;
    gap     = with_icon ? h * 0.24 : 0.0;

    /* 先量一下"想要多宽"，放不下就整块等比缩小（图标、字号、留白一起缩） */
    layout = make_layout(cr, text, FONT_MONO, font_px, PANGO_WEIGHT_BOLD, 0.0);
    pango_layout_get_pixel_size(layout, &tw, NULL);
    g_object_unref(layout);

    natural = (double)tw;
    want    = pad * 2.0 + icon + gap + natural;

    k = want > max_w ? max_w / want : 1.0;
    if (k < 0.62)
        k = 0.62;        /* 再小就看不清了，剩下的交给省略号 */

    hh      = h * k;
    cy      = y + (h - hh) * 0.5;
    pad    *= k;
    icon   *= k;
    gap    *= k;
    font_px *= k;

    layout = make_layout(cr, text, FONT_MONO, font_px, PANGO_WEIGHT_BOLD, 0.0);
    pango_layout_get_pixel_size(layout, &tw, &th);

    allowed = MAX(10.0, max_w - pad * 2.0 - icon - gap);
    if ((double)tw > allowed) {
        pango_layout_set_width(layout, (int)(allowed * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_get_pixel_size(layout, &tw, &th);
    }

    w = MIN(max_w, pad * 2.0 + icon + gap + (double)tw);
    x = cx - w * 0.5;

    if (with_icon) {
        platform_brand_color(platform, &r, &g, &b);

        cairo_set_source_rgba(cr, r, g, b, 0.16);
        rounded_rect(cr, x, cy, w, hh, hh * 0.5);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, r, g, b, 0.45);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        platform_draw_icon(cr, platform, x + pad, cy + (hh - icon) * 0.5, icon);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
        rounded_rect(cr, x, cy, w, hh, hh * 0.5);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, 0.902, 0.902, 0.910, 0.86);
    cairo_move_to(cr, x + pad + icon + gap, cy + (hh - (double)th) * 0.5);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

/* 一个二维码单元格：二维码 + 下方胶囊 */
static void draw_qr_cell(cairo_t *cr, QrSlot *slot, const Layout *L, double cx)
{
    cairo_surface_t *surface;
    double           total;

    if (slot->text == NULL)
        return;

    surface = qr_slot_surface(slot, L->qr_px);
    if (surface == NULL)
        return;

    total = (double)slot->pixels;

    cairo_set_source_surface(cr, surface, cx - total * 0.5, L->qr_y);
    cairo_paint(cr);

    draw_capsule(cr, slot->label, slot->platform, slot->with_icon,
                 cx, L->badge_y, L->badge_h,
                 MIN(L->cell_w, L->qr_px * 1.45));
}

/* ---------- 帮助页 ---------- */

static void draw_help(cairo_t *cr, double width, double height)
{
    static const struct { const char *key; const char *desc; } rows[] = {
        { "SPACE",   "播放 / 暂停" },
        { "←  →",    "后退 / 前进 5 秒（Shift = 30 秒）" },
        { "N / P",   "下一首 / 上一首" },
        { "R",       "从头重放当前曲目" },
        { "S",       "停止" },
        { "L",       "循环：关 → 列表 → 单曲" },
        { "H",       "随机播放开关" },
        { "[ / ]",   "歌词整体提前 / 延后 250ms（视频带片头时用）" },
        { "\\",      "歌词偏移恢复成 ini 里的值" },
        { "F / F11", "全屏切换" },
        { "F1",      "打开 / 关闭这一页" },
        { "Q",       "退出" },
        { "ESC",     "关闭这一页 / 退出全屏" },
    };
    double panel_w = clampd(width * 0.44, 320.0, 620.0);
    double row_h   = clampd(height * 0.048, 18.0, 34.0);
    double panel_h = row_h * ((double)G_N_ELEMENTS(rows) + 2.2);
    double panel_x = (width - panel_w) * 0.5;
    double panel_y = (height - panel_h) * 0.5;
    double key_x   = panel_x + panel_w * 0.10;
    double desc_x  = panel_x + panel_w * 0.40;

    if (panel_h > height * 0.94) {
        row_h   = height * 0.94 / ((double)G_N_ELEMENTS(rows) + 2.2);
        panel_h = row_h * ((double)G_N_ELEMENTS(rows) + 2.2);
        panel_y = (height - panel_h) * 0.5;
    }

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.62);
    cairo_rectangle(cr, 0.0, 0.0, width, height);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.071, 0.071, 0.078, 0.97);
    rounded_rect(cr, panel_x, panel_y, panel_w, panel_h, 10.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.902, 0.902, 0.910, 0.14);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    draw_text(cr, "SHORTCUTS", FONT_MONO, row_h * 0.62, PANGO_WEIGHT_NORMAL,
              row_h * 0.16, COL_DIM, key_x, panel_y + row_h * 0.75, FALSE);

    for (guint i = 0; i < G_N_ELEMENTS(rows); i++) {
        double y = panel_y + row_h * (1.9 + (double)i);

        draw_text(cr, rows[i].key, FONT_MONO, row_h * 0.52, PANGO_WEIGHT_BOLD,
                  row_h * 0.10, COL_TEXT, key_x, y, FALSE);
        draw_text(cr, rows[i].desc, FONT_MONO, row_h * 0.52, PANGO_WEIGHT_NORMAL,
                  0.0, COL_DIM, desc_x, y, FALSE);
    }
}

/* ---------- 自适应几何 ---------- */

static void layout_compute(Layout *L, cairo_t *cr, double w, double h,
                           const char *clock_text, int n_qr, gboolean need_lyric)
{
    const APPconfig  *cfg = config_get();
    const AudioTrack *track_for_note = ui.track >= 0
        ? config_track_at(cfg, (gsize)ui.track) : config_track_at(cfg, 0);
    double budget, texts, gaps, cover, k = 1.0;
    double clock_w, clock_avail;

    memset(L, 0, sizeof(*L));

    L->pad_x = clampd(w * 0.045, 12.0, 72.0);
    L->pad_y = clampd(h * 0.06, 10.0, 72.0);

    /* --- 右列宽度 --- */
    L->col_w = clampd(w * 0.32, 150.0, 460.0);
    if (L->col_w > w * 0.52)
        L->col_w = w * 0.52;
    if (L->col_w < 110.0)
        L->col_w = 110.0;

    /* 贴右边放，窗口再宽也不会离边缘太远 */
    L->col_cx = w - L->pad_x - L->col_w * 0.5;

    /* --- 二维码 --- */
    L->n_qr     = n_qr;
    L->cell_gap = L->col_w * 0.10;

    if (n_qr > 0) {
        L->cell_w = (n_qr == 2)
            ? (L->col_w - L->cell_gap) * 0.5
            : L->col_w;

        L->qr_px = clampd(h * 0.17, 64.0, 220.0);
        if (L->qr_px > L->cell_w)
            L->qr_px = L->cell_w;

        L->qr_px = floor(L->qr_px);      /* 取整：两个二维码尺寸绝对一致 */
    }

    /* --- 文字与胶囊 --- */
    L->now_px    = clampd(h * 0.030, 12.0, 34.0);
    L->lyric_px  = need_lyric ? clampd(h * 0.033, 12.0, 36.0) : 0.0;
    L->badge_h   = clampd(h * 0.042, 16.0, 34.0);
    L->badge_gap = h * 0.014;
    L->text_max  = L->cell_w;

    /* --- 竖向预算：封面吃掉剩下的空间，永远不溢出 --- */
    budget = h - 2.0 * L->pad_y;
    if (budget < 120.0)
        budget = MAX(80.0, h * 0.8);

    gaps  = h * 0.030 + h * 0.012 + (n_qr > 0 ? h * 0.045 : 0.0);
    texts = L->now_px * 1.45 + L->lyric_px * 1.45;
    if (n_qr > 0)
        texts += L->qr_px + L->badge_gap + L->badge_h;

    /* 窗口太矮：整块等比缩小，别让封面被挤成负数 */
    if (texts + 56.0 > budget) {
        k = MAX(0.45, (budget - 56.0) / MAX(texts, 1.0));

        L->now_px    *= k;
        L->lyric_px  *= k;
        L->badge_h   *= k;
        L->badge_gap *= k;
        L->qr_px     *= k;
        L->cell_w    *= k;
        L->text_max   = L->cell_w;
        texts        *= k;
        gaps         *= k;
    }

    cover = budget - texts - gaps;

    if (cover < 64.0 && L->lyric_px > 0.0) {
        /* 实在放不下：先牺牲歌词行 */
        cover += L->lyric_px * 1.45;
        L->lyric_px = 0.0;
    }

    L->cover_px = clampd(cover, 52.0, MIN(h * 0.34, L->col_w));

    L->cover_y = L->pad_y;
    L->now_y   = L->cover_y + L->cover_px + gaps * 0.45;
    L->lyric_y = L->now_y + L->now_px * 1.45 + gaps * 0.2;
    L->qr_y    = L->lyric_y + L->lyric_px * 1.45 + (n_qr > 0 ? gaps * 0.55 : 0.0);
    L->badge_y = L->qr_y + L->qr_px + L->badge_gap;

    /* --- 左列 --- */
    L->clock_px = clampd(h * 0.19, 24.0, 280.0);
    clock_avail = L->col_cx - L->col_w * 0.5 - L->pad_x * 1.6;

    clock_w = (double)measure_text(cr, clock_text, FONT_HAND, L->clock_px, PANGO_WEIGHT_NORMAL);
    if (clock_w > clock_avail && clock_w > 0.0)
        L->clock_px *= clock_avail / clock_w;

    if (L->clock_px < 18.0)
        L->clock_px = 18.0;

    /* 备注行（没有文字就不占高度） */
    {
        const gchar *note = config_track_note(config_get(), track_for_note);

        if (note != NULL) {
            L->note_px    = clampd(h * 0.036, 14.0, 42.0);
            L->note_h     = L->note_px * 1.42;
            L->note_y     = L->pad_y;
            L->note_max_w = MAX(80.0, clock_avail);
        }
    }

    /* 手写字体行高比字号大不少，所以这里用实测高度排进度线，避免压在字上 */
    L->clock_y = L->pad_y + (L->note_h > 0.0 ? L->note_h + h * 0.012 : 0.0)
                 - L->clock_px * 0.10;
    {
        double th = (double)measure_text_h(cr, clock_text, FONT_HAND,
                                           L->clock_px, PANGO_WEIGHT_NORMAL);

        L->bar_y = L->clock_y + th + h * 0.022;
    }

    L->bar_x = L->pad_x;
    L->bar_h = MAX(2.0, h * 0.0035);
    L->bar_w = MIN(w * 0.30, clock_avail);
    if (L->bar_w < 60.0)
        L->bar_w = MAX(40.0, clock_avail);
    L->dur_x  = L->bar_x + L->bar_w + w * 0.012;
    L->hint_y = MIN(h - MAX(14.0, h * 0.035), L->bar_y + h * 0.05);

    /* --- 左下角：墙钟 + 收听时长 + 12 周热力图 --- */
    L->wall_clock_px = clampd(h * 0.062, 12.0, 56.0);
    L->stat_px       = clampd(h * 0.022, 9.0, 17.0);
    L->heat_cell     = clampd(h * 0.011, 4.0, 11.0);
    L->heat_gap      = MAX(1.0, L->heat_cell * 0.28);
    L->heat_w        = 12.0 * (L->heat_cell + L->heat_gap) - L->heat_gap;
    L->heat_h        =  7.0 * (L->heat_cell + L->heat_gap) - L->heat_gap;

    {
        double block_h = L->wall_clock_px * 1.30 + h * 0.012
                       + L->stat_px * 1.60 + h * 0.012
                       + L->heat_h;

        L->stats_y = h - L->pad_y - block_h;

        /* 太矮就整体往上收，别和进度线打架 */
        if (L->stats_y < L->bar_y + h * 0.09)
            L->stats_y = L->bar_y + h * 0.09;
    }
}

/* ---------- 左下角：墙钟 / 收听时长 / 热力图 ---------- */

static void format_span(guint seconds, gchar *buf, gsize len)
{
    guint hours   = seconds / 3600;
    guint minutes = (seconds % 3600) / 60;

    if (hours > 0)
        g_snprintf(buf, len, "%uh %02um", hours, minutes);
    else
        g_snprintf(buf, len, "%um", minutes);
}

static void draw_stats_block(cairo_t *cr, const Layout *L)
{
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *clock_text = g_date_time_format(now, "%H:%M:%S");
    gchar       total_buf[32];
    gchar       today_buf[32];
    g_autofree gchar *label = NULL;
    double      x = L->pad_x;
    double      y = L->stats_y;
    int         max_seconds;
    double      seed_r = 0.60, seed_g = 0.70, seed_b = 0.95;

    format_span(stats_total_seconds(&ui.stats), total_buf, sizeof(total_buf));
    format_span(stats_seconds_today(&ui.stats), today_buf, sizeof(today_buf));
    label = g_strdup_printf("LISTENED %s  ·  TODAY %s", total_buf, today_buf);

    draw_text(cr, clock_text, FONT_HAND, L->wall_clock_px, PANGO_WEIGHT_NORMAL,
              0.0, COL_TEXT, x, y, FALSE);

    y += L->wall_clock_px * 1.30 + L->stat_px * 0.6;

    draw_text(cr, label, FONT_MONO, L->stat_px, PANGO_WEIGHT_NORMAL,
              MAX(0.4, L->stat_px * 0.14), COL_DIM, x, y, FALSE);

    y += L->stat_px * 1.60 + L->stat_px * 0.8;

    if (ui.palette.has) {
        seed_r = ui.palette.seed_r;
        seed_g = ui.palette.seed_g;
        seed_b = ui.palette.seed_b;
    }

    /* 12 列 x 7 行 = 最近 84 天，按星期几对齐（周一在最上面） */
    max_seconds = (int)stats_max_in_range(&ui.stats, 84);

    {
        int today_wd  = g_date_time_get_day_of_week(now) - 1;      /* 0 = 周一 */
        int start_wd  = ((today_wd - 83) % 7 + 7) % 7;

    for (int i = 0; i < 84; i++) {
        int    from_end = 83 - i;                     /* 0 = 最老的一天 */
        int    slot = start_wd + from_end;
        int    col = slot / 7;
        int    row = slot % 7;
        guint  seconds = stats_seconds_days_ago(&ui.stats, i);
        double level = 0.0;
        double cx = x + (double)col * (L->heat_cell + L->heat_gap);
        double cy = y + (double)row * (L->heat_cell + L->heat_gap);

        if (seconds > 0 && max_seconds > 0) {
            double ratio = (double)seconds / (double)max_seconds;

            level = 0.28 + 0.66 * MIN(1.0, ratio);
        }

        if (level <= 0.0)
            cairo_set_source_rgba(cr, 0.902, 0.902, 0.910, 0.07);
        else
            cairo_set_source_rgba(cr, seed_r, seed_g, seed_b, level);

        rounded_rect(cr, cx, cy, L->heat_cell, L->heat_cell, L->heat_cell * 0.22);
        cairo_fill(cr);
    }
    }
}

/* ---------- 主绘制 ---------- */

static void render(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    Layout            L;
    double            w = (double)width;
    double            h = (double)height;
    double            position, duration;
    g_autofree gchar *pos_text = NULL;
    g_autofree gchar *dur_text = NULL;
    int               n_qr;

    (void)area;
    (void)data;

    if (width <= 0 || height <= 0)
        return;

    position = media_position(&media);
    duration = media_duration(&media);
    pos_text = format_time(position);
    dur_text = format_time(duration);

    n_qr = (ui.qr[0].text != NULL ? 1 : 0) + (ui.qr[1].text != NULL ? 1 : 0);

    layout_compute(&L, cr, w, h, pos_text, n_qr,
                   ui.lyrics != NULL || ui.lyric_error != NULL || ui.lyrics_pending ||
                   media_is_loading(&media) || media_error(&media) != NULL);

    /* 背景不在这里画：它是下面那层 GtkPicture（小纹理 + GPU 放大），
     * 这层画布是透明的，只画文字 / 封面 / 二维码。 */

    /* ---------- 左列：备注 / 大时钟 / 细进度线 ---------- */
    if (L.note_h > 0.0) {
        const APPconfig  *cfg = config_get();
        const AudioTrack *track = ui.track >= 0 ? config_track_at(cfg, (gsize)ui.track) : NULL;
        const gchar      *note = config_track_note(cfg, track);

        if (note != NULL)
            draw_text_left(cr, note, FONT_MONO, L.note_px, PANGO_WEIGHT_NORMAL,
                           MAX(0.6, L.note_px * 0.16), L.note_max_w,
                           0.902, 0.902, 0.910, 0.62, L.pad_x, L.note_y);
    }

    draw_text(cr, pos_text, FONT_HAND, L.clock_px, PANGO_WEIGHT_NORMAL,
              0.0, COL_TEXT, L.pad_x, L.clock_y, FALSE);

    cairo_set_source_rgba(cr, COL_FAINT);
    cairo_rectangle(cr, L.bar_x, L.bar_y, L.bar_w, L.bar_h);
    cairo_fill(cr);

    if (duration > 0.0) {
        double frac = clampd(position / duration, 0.0, 1.0);

        cairo_set_source_rgba(cr, 0.902, 0.902, 0.910, 0.85);
        cairo_rectangle(cr, L.bar_x, L.bar_y, L.bar_w * frac, L.bar_h);
        cairo_fill(cr);
    }

    {
        double dur_px = MAX(9.0, L.now_px * 0.8);
        double th = (double)measure_text_h(cr, dur_text, FONT_MONO, dur_px, PANGO_WEIGHT_NORMAL);

        draw_text(cr, dur_text, FONT_MONO, dur_px, PANGO_WEIGHT_NORMAL,
                  0.0, COL_DIM, L.dur_x, L.bar_y + L.bar_h * 0.5 - th * 0.5, FALSE);
    }

    /* 播放模式 / 歌词偏移指示：只在非默认状态出现，不占版面 */
    if (ui.repeat != REPEAT_ALL || ui.shuffle || ui.lrc_offset_ms != 0) {
        GString *mode = g_string_new(NULL);

        if (ui.shuffle)
            g_string_append(mode, "SHUFFLE");

        if (ui.repeat != REPEAT_ALL)
            g_string_append_printf(mode, "%s%s", mode->len ? "  ·  " : "",
                                   ui.repeat == REPEAT_ONE ? "REPEAT ONE" : "REPEAT ALL");

        if (ui.lrc_offset_ms != 0)
            g_string_append_printf(mode, "%sLRC %+dms", mode->len ? "  ·  " : "",
                                   ui.lrc_offset_ms);

        draw_text(cr, mode->str, FONT_MONO, MAX(8.0, L.now_px * 0.62), PANGO_WEIGHT_NORMAL,
                  MAX(0.4, L.now_px * 0.12), COL_HINT,
                  L.pad_x, L.bar_y + MAX(10.0, h * 0.028), FALSE);

        g_string_free(mode, TRUE);
    }

    draw_stats_block(cr, &L);

    /* ---------- 右列：封面 / ♪ 曲名 ♪ / 歌词 / QR / 平台徽标 ---------- */
    {
        double      cover_x = L.col_cx - L.cover_px * 0.5;
        g_autofree gchar *label = current_label();

        draw_cover(cr, cover_x, L.cover_y, L.cover_px);

        if (label != NULL && *label != '\0') {
            g_autofree gchar *now = g_strdup_printf("♪ %s ♪", label);

            draw_text_ellipsized(cr, now, FONT_MONO, L.now_px, PANGO_WEIGHT_NORMAL,
                                 L.col_w, COL_TEXT, L.col_cx, L.now_y);
        }

        {
            const gchar *line = ui.lyric_line;
            double       r = 0.898, g = 0.898, b = 0.906, a = 0.78;

            if (media_error(&media) != NULL) {
                line = media_error(&media);
                r = 0.95; g = 0.55; b = 0.55; a = 0.85;
            } else if (ui.lyrics_pending) {
                line = "LYRICS...";
                a = 0.40;
            } else if (ui.lyric_error != NULL) {
                line = ui.lyric_error;
                r = 0.95; g = 0.72; b = 0.45; a = 0.85;
            } else if (media_is_loading(&media)) {
                line = "LOADING";
                a = 0.45;
            }

            if (line != NULL && *line != '\0' && L.lyric_px > 0.0)
                draw_text_ellipsized(cr, line, FONT_MONO, L.lyric_px, PANGO_WEIGHT_NORMAL,
                                     L.col_w, r, g, b, a, L.col_cx, L.lyric_y);
        }

        /* 二维码行：1 个居中，2 个并排 */
        if (n_qr > 0) {
            double row_w = (double)n_qr * L.cell_w + (double)(n_qr - 1) * L.cell_gap;
            double x0 = L.col_cx - row_w * 0.5;

            if (ui.qr[0].text != NULL)
                draw_qr_cell(cr, &ui.qr[0], &L,
                             n_qr == 2 ? x0 + L.cell_w * 0.5 : L.col_cx);

            if (ui.qr[1].text != NULL)
                draw_qr_cell(cr, &ui.qr[1], &L,
                             n_qr == 2 ? x0 + L.cell_w + L.cell_gap + L.cell_w * 0.5
                                       : L.col_cx);
        }
    }

    if (ui.help)
        draw_help(cr, w, h);
}

/* ---------- 曲目附加信息 ---------- */

/* 在音频所在目录里找一个 .lrc：精确同名 → 同前缀 → 目录里唯一一个 */
static gchar *find_lrc_in_dir(const char *dir, const char *stem)
{
    g_autoptr(GDir) d = NULL;
    const gchar    *name;
    gchar          *prefix_hit = NULL;
    gchar          *only_hit = NULL;
    gint            count = 0;

    if (dir == NULL || stem == NULL)
        return NULL;

    d = g_dir_open(dir, 0, NULL);
    if (d == NULL)
        return NULL;

    while ((name = g_dir_read_name(d)) != NULL) {
        g_autofree gchar *full = NULL;

        if (!g_str_has_suffix(name, ".lrc"))
            continue;

        count++;
        full = g_build_filename(dir, name, NULL);

        if (g_str_has_prefix(name, stem)) {
            g_free(prefix_hit);
            prefix_hit = g_steal_pointer(&full);
        } else if (only_hit == NULL) {
            only_hit = g_strdup(full);
        }
    }

    if (prefix_hit != NULL)
        return prefix_hit;

    if (count == 1)
        return only_hit;

    g_free(only_hit);
    return NULL;
}

/* ---------- 在线歌词源 ---------- */

typedef struct {
    guint  gen;
    gchar *provider;
    gchar *spec;
    gchar *artist;
    gchar *title;
    gchar    *text;      /* 取回来的 LRC 文本 */
    gchar    *error;
    gboolean  from_cache;
} LyricsJob;

/* 缓存目录：<user cache>/follow/lyrics-<md5>.lrc */
static gchar *lyrics_cache_path(const LyricsJob *job)
{
    g_autofree gchar *key = g_strdup_printf("%s|%s|%s|%s",
                                            job->provider,
                                            job->spec ? job->spec : "",
                                            job->artist ? job->artist : "",
                                            job->title ? job->title : "");

    return follow_cache_file("lyrics", key, "lrc");
}

static gboolean lyrics_done(gpointer data)
{
    LyricsJob *job = data;

    if (job->gen == ui.lyrics_gen) {
        ui.lyrics_pending = FALSE;

        if (job->text != NULL) {
            if (ui.lyrics != NULL) {
                lyrics_free(ui.lyrics);
                ui.lyrics = NULL;
            }

            ui.lyrics = lyrics_load_text(job->text);

            if (ui.lyrics == NULL) {
                g_free(ui.lyric_error);
                ui.lyric_error = g_strdup("歌词源返回的内容没有时间轴");
            } else {
                g_free(ui.lyric_error);
                ui.lyric_error = NULL;
                g_message("lyrics: %" G_GSIZE_FORMAT " lines (%s)%s",
                          ui.lyrics->n_lines, ui.lyrics_spec ? ui.lyrics_spec : "?",
                          job->from_cache ? " [cache]" : "");
            }
        } else {
            g_free(ui.lyric_error);
            ui.lyric_error = g_strdup(job->error != NULL ? job->error : "取歌词失败");
        }

        request_redraw();
    }

    g_free(job->provider);
    g_free(job->spec);
    g_free(job->artist);
    g_free(job->title);
    g_free(job->text);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer lyrics_thread(gpointer data)
{
    LyricsJob *job = data;
    gchar     *cached = lyrics_cache_path(job);

    if (g_file_test(cached, G_FILE_TEST_EXISTS)) {
        if (!g_file_get_contents(cached, &job->text, NULL, NULL))
            job->text = NULL;
        job->from_cache = TRUE;
    } else {
        LyricsQuery query = { job->provider, job->spec, job->artist, job->title };

        job->text = lyricsrc_fetch(&query, &job->error);

        if (job->text != NULL)
            g_file_set_contents(cached, job->text, -1, NULL);
    }

    g_free(cached);

    g_idle_add(lyrics_done, job);
    return NULL;
}

/* 音频时长和歌词总长差得多 → 提示可能是不同版本（片头/剪辑/TV-size） */
static void lyrics_report_fit(void)
{
    double dur, lrc_end, diff;

    if (ui.lyrics == NULL || ui.lyrics->n_lines == 0)
        return;

    dur = media_duration(&media);
    if (dur <= 0.0)
        return;

    lrc_end = (double)ui.lyrics->lines[ui.lyrics->n_lines - 1].time_ms / 1000.0;
    diff    = dur - lrc_end;

    if (lrc_end > dur * 1.15) {
        g_warning("lyrics: 歌词（%.0fs）比音频（%.0fs）长很多，大概率不是同一个版本，"
                  "换个歌词源更靠谱（lrc=qq:... 或本地 .lrc）", lrc_end, dur);
    } else if (diff > 8.0) {
        g_message("lyrics: 音频比歌词长 %.0fs（片头/片尾？）—— 用 [ 或 ] 调偏移，"
                  "或用 lrc_offset=%.0f 固定下来", diff, diff * 1000.0);
    } else if (diff < -8.0) {
        g_message("lyrics: 歌词比音频长 %.0fs —— 用 ] 把歌词提前，或 lrc_offset=%.0f",
                  -diff, diff * 1000.0);
    }
}

static void load_lyrics(const AudioTrack *track)
{
    g_autofree gchar *audio = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *stem = NULL;
    g_autofree gchar *chosen = NULL;
    g_autofree gchar *candidate = NULL;

    g_free(ui.lyric_line);
    ui.lyric_line = NULL;

    g_free(ui.lyric_error);
    ui.lyric_error = NULL;

    if (ui.lyrics != NULL) {
        lyrics_free(ui.lyrics);
        ui.lyrics = NULL;
    }

    if (track == NULL)
        return;

    ui.lyrics_gen++;

    /* 在线歌词源：lrc=netease:... / lrc=qq:... */
    if (track->lrc != NULL && track->lrc[0] != 0) {
        g_autofree gchar *provider = NULL;
        g_autofree gchar *spec = NULL;

        if (lyricsrc_parse(track->lrc, &provider, &spec)) {
            LyricsJob        *job = g_new0(LyricsJob, 1);
            g_autofree gchar *derived = NULL;

            job->gen      = ui.lyrics_gen;
            job->provider = g_steal_pointer(&provider);
            job->spec     = g_steal_pointer(&spec);
            job->artist   = g_strdup(track->artist);
            job->title    = g_strdup(track->title);

            /* 没写 title 就退回文件名（只对本地曲目有意义） */
            if (job->title == NULL && track->link != NULL && !track->link_remote) {
                derived = config_track_title(track);
                job->title = g_steal_pointer(&derived);
            }

            g_free(ui.lyrics_spec);
            ui.lyrics_spec    = g_strdup(track->lrc);
            ui.lyrics_pending = TRUE;

            g_message("lyrics: resolving %s", track->lrc);
            g_thread_unref(g_thread_new("follow-lyrics", lyrics_thread, job));
            return;
        }
    }

    /* 本地音频的目录和文件名（不含扩展名），用来找同名 .lrc、也用来解析相对路径 */
    if (track->link != NULL && !track->link_remote) {
        audio = config_to_local_path(track->link);

        if (audio != NULL) {
            gchar *dot;

            dir  = g_path_get_dirname(audio);
            stem = g_path_get_basename(audio);

            dot = strrchr(stem, '.');
            if (dot != NULL && dot != stem)
                *dot = '\0';
        }
    }

    /* 1) ini 里的 lrc= */
    if (track->lrc != NULL && *track->lrc != '\0') {
        if (track->lrc_remote) {
            ui.lyric_error = g_strdup("远程 .lrc 还不会下载");
        } else {
            candidate = config_to_local_path(track->lrc);

            if (candidate != NULL && g_file_test(candidate, G_FILE_TEST_EXISTS)) {
                chosen = g_steal_pointer(&candidate);
            } else if (dir != NULL && !g_path_is_absolute(track->lrc)) {
                /* 相对路径：相对当前目录找不到，就按音频所在目录再试一次 */
                g_autofree gchar *alt = g_build_filename(dir, track->lrc, NULL);

                if (g_file_test(alt, G_FILE_TEST_EXISTS))
                    chosen = g_steal_pointer(&alt);
            }

            if (chosen == NULL)
                ui.lyric_error = g_strdup_printf("找不到歌词：%s", track->lrc);
        }
    }

    /* 2) 音频旁边的同名 .lrc（ini 没写、或者写了但文件不存在） */
    if (chosen == NULL && dir != NULL && stem != NULL)
        chosen = find_lrc_in_dir(dir, stem);

    if (chosen == NULL)
        return;      /* 没有歌词就安静地不显示 */

    ui.lyrics = lyrics_load_file(chosen);

    if (ui.lyrics == NULL) {
        g_free(ui.lyric_error);
        ui.lyric_error = g_strdup_printf("歌词解析失败：%s", chosen);
    } else {
        g_free(ui.lyric_error);
        ui.lyric_error = NULL;
        g_message("lyrics: %" G_GSIZE_FORMAT " lines from %s", ui.lyrics->n_lines, chosen);
    }
}

/* 二维码规则：
 *   曲目页（page / 网络 link）→ 只有网络地址才显示，胶囊用平台徽标
 *   [qrcode] link            → 只有网络地址才显示，胶囊显示地址
 */
static void set_links(const AudioTrack *track)
{
    const APPconfig *cfg = config_get();
    const gchar     *page = NULL;

    if (track != NULL) {
        if (track->page != NULL && *track->page != '\0' && config_is_remote(track->page))
            page = track->page;
        else if (track->link_remote)
            page = track->link;
    }

    if (page != NULL) {
        Platform platform = PLATFORM_UNKNOWN;

        if (track != NULL && track->platform != NULL)
            platform = platform_from_name(track->platform);
        if (platform == PLATFORM_UNKNOWN)
            platform = platform_from_url(page);

        qr_slot_set(&ui.qr[0], page, platform_name(platform), platform, TRUE);
    } else {
        qr_slot_clear(&ui.qr[0]);
    }

    /* 自定义链接：本地路径就不显示二维码 */
    if (cfg->qrcode_link != NULL && config_is_remote(cfg->qrcode_link)) {
        if (page != NULL && g_strcmp0(page, cfg->qrcode_link) == 0) {
            qr_slot_clear(&ui.qr[1]);        /* 同一个地址不重复显示 */
        } else {
            g_autofree gchar *host = url_host(cfg->qrcode_link);

            qr_slot_set(&ui.qr[1], cfg->qrcode_link,
                        cfg->qrcode_label != NULL ? cfg->qrcode_label : host,
                        PLATFORM_UNKNOWN, FALSE);
        }
    } else {
        qr_slot_clear(&ui.qr[1]);
    }

    request_redraw();
}

static void set_resolved_meta(const gchar *title, const gchar *artist)
{
    g_free(ui.resolved_title);
    g_free(ui.resolved_artist);
    ui.resolved_title  = title != NULL ? g_strdup(title) : NULL;
    ui.resolved_artist = artist != NULL ? g_strdup(artist) : NULL;
    request_redraw();
}

/* 显示用的「艺人 - 曲名」。
 * 优先级：ini 里的 title/artist > 网络解析出来的 > 文件名。
 * 这样 B 站的视频标题不会盖掉你手写的曲名。 */
static gchar *current_label(void)
{
    const APPconfig  *cfg = config_get();
    const AudioTrack *track = ui.track >= 0 ? config_track_at(cfg, (gsize)ui.track) : NULL;
    const gchar      *artist = NULL;
    const gchar      *title = NULL;
    g_autofree gchar *fallback = NULL;

    if (track != NULL && track->artist != NULL && *track->artist != '\0')
        artist = track->artist;
    else if (ui.resolved_artist != NULL && *ui.resolved_artist != '\0')
        artist = ui.resolved_artist;

    if (track != NULL && track->title != NULL && *track->title != '\0')
        title = track->title;
    else if (ui.resolved_title != NULL && *ui.resolved_title != '\0')
        title = ui.resolved_title;

    if (title == NULL && track != NULL) {
        fallback = config_track_title(track);
        title = fallback;
    }

    if (artist != NULL && title != NULL && *title != '\0')
        return g_strdup_printf("%s - %s", artist, title);
    if (title != NULL && *title != '\0')
        return g_strdup(title);
    if (artist != NULL && *artist != '\0')
        return g_strdup(artist);

    return NULL;
}

static void update_window_title(void)
{
    g_autofree gchar *label = current_label();

    if (label != NULL && *label != '\0') {
        g_autofree gchar *title = g_strdup_printf("%s  —  Follow", label);

        gtk_window_set_title(GTK_WINDOW(ui.window), title);
    } else {
        gtk_window_set_title(GTK_WINDOW(ui.window), "Follow");
    }
}

/* ---------- 播放 ---------- */

typedef struct {
    guint       gen;
    MediaSource src;
    gchar      *uri;
    gchar      *error;
} ResolveJob;

static gboolean resolve_done(gpointer data)
{
    ResolveJob *job = data;

    if (job->gen == ui.resolve_gen) {
        if (job->src.url != NULL) {
            if (media_play(&media, job->src.url, job->src.headers) == 0) {
                ui.shown_second = -1;

                {
                    const APPconfig  *cfg = config_get();
                    const AudioTrack *t = ui.track >= 0
                        ? config_track_at(cfg, (gsize)ui.track) : NULL;
                    gboolean has_title  = t != NULL && t->title != NULL && *t->title != '\0';
                    gboolean has_artist = t != NULL && t->artist != NULL && *t->artist != '\0';

                    /* ini 写了就用 ini 的，只有缺的字段才用解析结果补 */
                    if (!has_title || !has_artist) {
                        set_resolved_meta(has_title  ? NULL : job->src.title,
                                          has_artist ? NULL : job->src.artist);
                    }
                }

                update_window_title();
                request_redraw();
            }
        } else {
            g_warning("play: %s", job->error != NULL ? job->error : "解析失败");
        }
    }

    source_clear(&job->src);
    g_free(job->uri);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer resolve_thread(gpointer data)
{
    ResolveJob *job = data;

    if (source_resolve(job->uri, &job->src, &job->error) != 0)
        job->error = job->error != NULL ? job->error : g_strdup("解析失败");

    g_free(job->uri);
    job->uri = NULL;

    g_idle_add(resolve_done, job);
    return NULL;
}

static gboolean play_track_index(gint index)
{
    const APPconfig  *cfg = config_get();
    const AudioTrack *track;
    g_autofree gchar *local = NULL;

    if (index < 0 || (gsize)index >= cfg->n_tracks)
        return FALSE;

    track = config_track_at(cfg, (gsize)index);
    if (track == NULL)
        return FALSE;

    ui.track = index;
    ui.shown_second = -1;
    history_push(index);

    /* 换歌时把歌词偏移重置回 ini 里这个曲目的值 */
    ui.lrc_offset_base   = track->lrc_offset_ms;
    ui.lrc_offset_ms     = track->lrc_offset_ms;
    ui.lyrics_fit_checked = FALSE;

    set_cover(track);
    load_lyrics(track);
    set_links(track);
    set_resolved_meta(NULL, NULL);
    update_window_title();
    request_redraw();

    if (track->link_remote) {
        ResolveJob *job = g_new0(ResolveJob, 1);

        job->gen = ++ui.resolve_gen;
        job->uri = g_strdup(track->link);

        g_message("play: track[%d] %s -> resolving %s", index, track->id, track->link);
        g_thread_unref(g_thread_new("follow-resolve", resolve_thread, job));
        return TRUE;
    }

    local = config_to_local_path(track->link);
    if (local == NULL || !g_file_test(local, G_FILE_TEST_EXISTS)) {
        g_warning("track %s: audio file not found: %s (cwd = %s)",
                  track->id, track->link, g_get_current_dir());
        return FALSE;
    }

    if (media_play(&media, local, NULL) != 0) {
        g_warning("track %s: cannot play %s", track->id, local);
        return FALSE;
    }

    g_message("play: track[%d] %s -> %s", index, track->id, local);
    return TRUE;
}

static void history_push(gint index)
{
    if (ui.history_len < (gint)G_N_ELEMENTS(ui.history))
        ui.history[ui.history_len++] = index;
}

/* 随机挑一个和当前不同的曲目；失败就退回顺序扫描 */
static gboolean play_random(void)
{
    const APPconfig *cfg = config_get();
    gint             n = (gint)cfg->n_tracks;

    if (n <= 0)
        return FALSE;

    if (n == 1)
        return play_track_index(0);

    for (gint attempt = 0; attempt < n * 3; attempt++) {
        gint idx = g_random_int_range(0, n);

        if (idx == ui.track)
            continue;

        if (play_track_index(idx))
            return TRUE;
    }

    return FALSE;
}

static void play_neighbour(gint dir)
{
    const APPconfig *cfg = config_get();
    gint             n = (gint)cfg->n_tracks;
    gint             start = ui.track;

    if (n <= 0) {
        g_warning("config: no playable [audioN] section in %s", CONFIG_PATH);
        return;
    }

    /* 随机模式：下一首随机；上一首优先走历史 */
    if (ui.shuffle) {
        if (dir < 0 && ui.history_len > 1) {
            ui.history_len--;            /* 栈顶是当前曲目，先弹掉 */

            for (gint i = ui.history_len - 1; i >= 0; i--) {
                if (play_track_index(ui.history[i]))
                    return;              /* play_track_index 会把它重新压栈 */
            }
        }

        if (play_random())
            return;

        g_warning("shuffle: no playable track, falling back to sequential");
    }

    for (gint k = 1; k <= n; k++) {
        gint idx = (start + k * dir) % n;

        if (idx < 0)
            idx += n;

        if (play_track_index(idx))
            return;

    }

    g_warning("playlist: no playable track found");
}

static void cycle_repeat(void)
{
    ui.repeat = (ui.repeat + 1) % 3;
    g_message("repeat: %s", config_repeat_name(ui.repeat));
    request_redraw();
}

static void toggle_shuffle(void)
{
    ui.shuffle = !ui.shuffle;

    if (ui.shuffle && ui.track >= 0) {
        ui.history_len = 0;
        history_push(ui.track);
    }

    g_message("shuffle: %s", ui.shuffle ? "on" : "off");
    request_redraw();
}

/* 一首放完之后按模式决定下一步 */
static void on_track_finished(void)
{
    if (ui.repeat == REPEAT_ONE) {
        if (media.has_stream) {
            media_seek(&media, 0.0);
            media_continue(&media);
        } else {
            play_track_index(ui.track);
        }
        return;
    }

    if (ui.shuffle) {
        if (play_random())
            return;
    }

    if (ui.repeat == REPEAT_ALL) {
        play_neighbour(+1);
        return;
    }

    /* 顺序播放且不循环：最后一首就停下 */
    {
        const APPconfig *cfg = config_get();

        if (ui.track + 1 < (gint)cfg->n_tracks) {
            play_neighbour(+1);
        } else {
            g_message("playlist: reached the end");
            media_stop(&media);
            request_redraw();
        }
    }
}

static void play_first_playable(void)
{
    const APPconfig *cfg = config_get();

    if (cfg->n_tracks == 0) {
        g_warning("config: no playable [audioN] section in %s", CONFIG_PATH);
        return;
    }

    for (gsize i = 0; i < cfg->n_tracks; i++) {
        if (play_track_index((gint)i))
            return;
    }

    g_warning("Play music failed: none of the %" G_GSIZE_FORMAT
              " track(s) in %s is playable", cfg->n_tracks, CONFIG_PATH);
}

static void toggle_play(void)
{
    if (!media.ready) {
        g_warning("Media engine not ready!");
        return;
    }

    if (media_is_playing(&media)) {
        media_pause(&media);
        request_redraw();
        return;
    }

    if (media_is_paused(&media)) {
        media_continue(&media);
        request_redraw();
        return;
    }

    if (media.has_stream && ui.track >= 0) {
        media_continue(&media);
        request_redraw();
        return;
    }

    play_first_playable();
}

static void seek_by(double delta)
{
    double duration = media_duration(&media);
    double target;

    if (!media.ready || duration <= 0.0)
        return;

    target = clampd(media_position(&media) + delta, 0.0, MAX(duration - 0.5, 0.0));
    media_seek(&media, target);
    request_redraw();
}

static void toggle_fullscreen(void)
{
    GtkWindow *window = GTK_WINDOW(ui.window);

    if (gtk_window_is_fullscreen(window))
        gtk_window_unfullscreen(window);
    else
        gtk_window_fullscreen(window);
}

/* ---------- 每帧刷新 ---------- */

static gboolean on_tick(gpointer data)
{
    int         width  = gtk_widget_get_width(ui.window);
    int         height = gtk_widget_get_height(ui.window);
    int         second;
    gboolean    playing, paused;
    const gchar *line;

    (void)data;

    if (media_is_finished(&media))
        on_track_finished();

    playing = media_is_playing(&media);
    paused  = media_is_paused(&media);

    /* 收听时长：按墙上时间累计，异常大跳变（睡眠/卡顿）丢掉 */
    if (playing) {
        gint64 now_us = g_get_real_time();

        if (ui.stats_last_us > 0) {
            double dt = (double)(now_us - ui.stats_last_us) / 1000000.0;

            if (dt > 0.0 && dt < 5.0)
                stats_add(&ui.stats, dt);
        }

        ui.stats_last_us = now_us;

        if (now_us - ui.stats_saved_us > 30 * G_USEC_PER_SEC) {
            stats_save(&ui.stats);
            ui.stats_saved_us = now_us;
        }
    } else {
        ui.stats_last_us = 0;
    }

    if (width != ui.last_width || height != ui.last_height) {
        ui.last_width  = width;
        ui.last_height = height;
        request_redraw();
    }

    second = (int)media_position(&media);
    if (second != ui.shown_second) {
        ui.shown_second = second;
        request_redraw();
    }

    if (playing != ui.last_playing || paused != ui.last_paused) {
        ui.last_playing = playing;
        ui.last_paused  = paused;
        request_redraw();
    }

    /* 歌词时间轴按"原始音频"做的；视频源可能带片头/剪辑，
     * 所以这里用 offset 把播放位置平移一下（正数 = 歌词延后） */
    if (!ui.lyrics_fit_checked && ui.lyrics != NULL && media_duration(&media) > 0.0) {
        ui.lyrics_fit_checked = TRUE;
        lyrics_report_fit();
    }

    line = lyrics_line_at(ui.lyrics,
                          media_position(&media) - (double)ui.lrc_offset_ms / 1000.0);
    if ((line == NULL) != (ui.lyric_line == NULL) ||
        (line != NULL && ui.lyric_line != NULL && strcmp(line, ui.lyric_line) != 0)) {
        g_free(ui.lyric_line);
        ui.lyric_line = line != NULL ? g_strdup(line) : NULL;
        request_redraw();
    }

    if (ui.dirty) {
        ui.dirty = FALSE;
        gtk_widget_queue_draw(ui.canvas);
    }

    return G_SOURCE_CONTINUE;
}

/* ---------- 键盘 ---------- */

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                              guint keycode, GdkModifierType state, gpointer data)
{
    (void)controller;
    (void)keycode;
    (void)data;

    if (ui.help) {
        if (keyval == GDK_KEY_F1 || keyval == GDK_KEY_Escape) {
            ui.help = FALSE;
            request_redraw();
            return TRUE;
        }
        if (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)
            gtk_window_close(GTK_WINDOW(ui.window));
        return TRUE;
    }

    switch (keyval) {
    case GDK_KEY_space:
        toggle_play();
        return TRUE;
    case GDK_KEY_Left:
        seek_by((state & GDK_SHIFT_MASK) ? -30.0 : -5.0);
        return TRUE;
    case GDK_KEY_Right:
        seek_by((state & GDK_SHIFT_MASK) ? 30.0 : 5.0);
        return TRUE;
    case GDK_KEY_n: case GDK_KEY_N:
        play_neighbour(+1);
        return TRUE;
    case GDK_KEY_p: case GDK_KEY_P:
        play_neighbour(-1);
        return TRUE;
    case GDK_KEY_r: case GDK_KEY_R:
        if (ui.track >= 0)
            play_track_index(ui.track);
        else
            play_first_playable();
        return TRUE;
    case GDK_KEY_s: case GDK_KEY_S:
        media_stop(&media);
        request_redraw();
        return TRUE;
    case GDK_KEY_bracketleft:          /* 歌词提前 250ms */
        ui.lrc_offset_ms -= 250;
        g_message("lyrics offset: %+d ms", ui.lrc_offset_ms);
        request_redraw();
        return TRUE;
    case GDK_KEY_bracketright:         /* 歌词延后 250ms */
        ui.lrc_offset_ms += 250;
        g_message("lyrics offset: %+d ms", ui.lrc_offset_ms);
        request_redraw();
        return TRUE;
    case GDK_KEY_backslash:            /* 恢复 ini 里的值 */
        ui.lrc_offset_ms = ui.lrc_offset_base;
        g_message("lyrics offset: reset to %+d ms", ui.lrc_offset_ms);
        request_redraw();
        return TRUE;
    case GDK_KEY_l: case GDK_KEY_L:
        cycle_repeat();
        return TRUE;
    case GDK_KEY_h: case GDK_KEY_H:
        toggle_shuffle();
        return TRUE;
    case GDK_KEY_f: case GDK_KEY_F: case GDK_KEY_F11:
        toggle_fullscreen();
        return TRUE;
    case GDK_KEY_F1:
        ui.help = TRUE;
        request_redraw();
        return TRUE;
    case GDK_KEY_Escape:
        if (gtk_window_is_fullscreen(GTK_WINDOW(ui.window)))
            gtk_window_unfullscreen(GTK_WINDOW(ui.window));
        return TRUE;
    case GDK_KEY_q: case GDK_KEY_Q:
        gtk_window_close(GTK_WINDOW(ui.window));
        return TRUE;
    default:
        return FALSE;
    }
}

/* ---------- 构建界面 ---------- */

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget          *window;
    GtkWidget          *header_bar;
    GtkEventController *keys;
    const APPconfig    *cfg = config_get();

    (void)user_data;

    window = gtk_application_window_new(app);
    ui.window = window;

    header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    gtk_window_set_title(GTK_WINDOW(window), cfg->title);
    gtk_window_set_default_size(GTK_WINDOW(window), cfg->width, cfg->height);

    if (cfg->fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(window));

    /* 底：动态背景（小纹理，GPU 拉伸） */
    ui.bg_picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(ui.bg_picture), GTK_CONTENT_FIT_FILL);
    gtk_picture_set_can_shrink(GTK_PICTURE(ui.bg_picture), TRUE);
    gtk_widget_set_hexpand(ui.bg_picture, TRUE);
    gtk_widget_set_vexpand(ui.bg_picture, TRUE);
    gtk_widget_add_css_class(ui.bg_picture, "bg-picture");

    /* 上：文字 / 封面 / 二维码（透明画布） */
    ui.canvas = gtk_drawing_area_new();
    gtk_widget_add_css_class(ui.canvas, "canvas");
    gtk_widget_set_hexpand(ui.canvas, TRUE);
    gtk_widget_set_vexpand(ui.canvas, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui.canvas), render, NULL, NULL);

    {
        GtkWidget *overlay = gtk_overlay_new();

        gtk_overlay_set_child(GTK_OVERLAY(overlay), ui.bg_picture);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ui.canvas);
        gtk_window_set_child(GTK_WINDOW(window), overlay);
    }

    keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
    gtk_widget_add_controller(window, keys);

    ui.track = -1;
    ui.shown_second = -1;
    ui.repeat      = cfg->repeat;
    ui.shuffle     = cfg->shuffle;
    ui.history_len = 0;
    set_cover(config_track_at(cfg, 0));
    set_links(config_track_at(cfg, 0));
    update_window_title();

    gtk_window_present(GTK_WINDOW(window));

    ui.tick_id = g_timeout_add(TICK_MS, on_tick, NULL);

    /* 背景动画：~20fps，只重画 320x180 的小纹理 */
    ui.bg_phase = 0.0;
    push_bg_frame(cfg->width, cfg->height);
    ui.bg_tick_id = g_timeout_add(33, on_bg_tick, NULL);
}

/* ---------- 生命周期 ---------- */

static void startup(GtkApplication *app, gpointer user_data)
{
    GtkCssProvider *provider;

    (void)app;
    (void)user_data;

    http_global_init();

    if (!g_file_test(CSS_PATH, G_FILE_TEST_EXISTS))
        g_warning("CSS file is not found in: %s", CSS_PATH);

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, CSS_PATH);

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);

    if (media_init(&media) != 0)
        g_warning("Media engine init failed!");

    if (!g_file_test(CONFIG_PATH, G_FILE_TEST_EXISTS))
        g_warning("Config file is not found in: %s (using defaults)", CONFIG_PATH);
    else
        g_message("config: loading %s (cwd = %s)", CONFIG_PATH, g_get_current_dir());

    config_load(CONFIG_PATH);

    stats_init(&ui.stats);
    stats_load(&ui.stats);

    {
        const APPconfig *cfg = config_get();

        g_message("config: %" G_GSIZE_FORMAT " track(s), window %dx%d%s",
                  cfg->n_tracks, cfg->width, cfg->height,
                  cfg->fullscreen ? " (fullscreen)" : "");

        for (gsize i = 0; i < cfg->n_tracks; i++) {
            const AudioTrack *t = config_track_at(cfg, i);
            g_autofree gchar *label = config_track_label(t);

            g_message("track[%" G_GSIZE_FORMAT "] %s  %s  link=%s%s  page=%s%s  lrc=%s",
                      i, t->id, label != NULL ? label : "-",
                      t->link, t->link_remote ? " (remote)" : "",
                      t->page != NULL ? t->page : "-",
                      t->platform != NULL ? t->platform : "",
                      t->lrc != NULL ? t->lrc : "-");
        }

        g_message("config: custom qr = %s",
                  (cfg->qrcode_link != NULL && config_is_remote(cfg->qrcode_link))
                      ? cfg->qrcode_link : "(local/none, hidden)");
    }
}

static void shutdown_app(GtkApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;

    if (ui.tick_id != 0) {
        g_source_remove(ui.tick_id);
        ui.tick_id = 0;
    }

    if (ui.bg_tick_id != 0) {
        g_source_remove(ui.bg_tick_id);
        ui.bg_tick_id = 0;
    }

    stats_save(&ui.stats);
    stats_clear(&ui.stats);

    media_clear(&media);
    config_free();

    g_free(ui.lyric_line);
    g_free(ui.lyric_error);
    g_free(ui.lyrics_spec);
    g_free(ui.resolved_title);
    g_free(ui.resolved_artist);

    if (ui.lyrics != NULL)
        lyrics_free(ui.lyrics);

    if (ui.cover != NULL)
        cairo_surface_destroy(ui.cover);
    if (ui.cover_scaled != NULL)
        cairo_surface_destroy(ui.cover_scaled);

    drop_background();

    qr_slot_clear(&ui.qr[0]);
    qr_slot_clear(&ui.qr[1]);

    http_global_cleanup();
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    (void)argc;

    app = gtk_application_new("org.github.clevebitr.follow",
                              G_APPLICATION_DEFAULT_FLAGS);
    if (app == NULL)
        return 1;

    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(shutdown_app), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}
