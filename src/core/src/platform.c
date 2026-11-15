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

#include <math.h>
#include <string.h>

#include <librsvg/rsvg.h>

#include "platform.h"

/* 图标来自 Simple Icons（CC0）；运行时由 librsvg 渲染为白色遮罩。 */

static const struct {
    const char *id;
    const char *name;
    const char *slug;
    double      r, g, b;
    const char *hosts[5];
} g_platforms[] = {
    { "link", "LINK", NULL, 0.35, 0.35, 0.38, { NULL } },

    { "bilibili", "BILIBILI", "bilibili", 0.984, 0.447, 0.600,
      { "bilibili.com", "b23.tv", "bili2233.cn", "bilibili.tv", NULL } },

    { "applemusic", "APPLE MUSIC", "applemusic", 0.980, 0.176, 0.282,
      { "music.apple.com", "itunes.apple.com", "apple.co", NULL, NULL } },

    { "youtube", "YOUTUBE", "youtube", 1.000, 0.000, 0.000,
      { "youtube.com", "youtu.be", "music.youtube.com", NULL, NULL } },

    { "spotify", "SPOTIFY", "spotify", 0.114, 0.725, 0.329,
      { "spotify.com", "spoti.fi", "open.spotify.com", NULL, NULL } },

    { "soundcloud", "SOUNDCLOUD", "soundcloud", 1.000, 0.333, 0.000,
      { "soundcloud.com", "snd.sc", NULL, NULL, NULL } },

    { "netease", "NETEASE", "neteasecloudmusic", 0.760, 0.047, 0.047,
      { "music.163.com", "163cn.tv", "y.music.163.com", NULL, NULL } },

    { "bandcamp", "BANDCAMP", "bandcamp", 0.114, 0.627, 0.765,
      { "bandcamp.com", NULL, NULL, NULL, NULL } },
};

#define PLATFORM_COUNT ((int)G_N_ELEMENTS(g_platforms))

Platform platform_from_url(const char *url)
{
    if (url == NULL || *url == '\0')
        return PLATFORM_UNKNOWN;

    for (int p = 1; p < PLATFORM_COUNT; p++) {
        for (int h = 0; h < 5 && g_platforms[p].hosts[h] != NULL; h++) {
            if (g_strstr_len(url, -1, g_platforms[p].hosts[h]) != NULL)
                return (Platform)p;
        }
    }

    return PLATFORM_UNKNOWN;
}

Platform platform_from_name(const char *name)
{
    if (name == NULL || *name == '\0')
        return PLATFORM_UNKNOWN;

    for (int p = 1; p < PLATFORM_COUNT; p++) {
        if (g_ascii_strcasecmp(name, g_platforms[p].id) == 0)
            return (Platform)p;
    }

    if (g_ascii_strcasecmp(name, "b站") == 0 ||
        g_ascii_strcasecmp(name, "bili") == 0)
        return PLATFORM_BILIBILI;
    if (g_ascii_strcasecmp(name, "apple") == 0 ||
        g_ascii_strcasecmp(name, "am") == 0 ||
        g_ascii_strcasecmp(name, "apple music") == 0)
        return PLATFORM_APPLE_MUSIC;
    if (g_ascii_strcasecmp(name, "yt") == 0)
        return PLATFORM_YOUTUBE;
    if (g_ascii_strcasecmp(name, "wyy") == 0 ||
        g_ascii_strcasecmp(name, "网易云") == 0)
        return PLATFORM_NETEASE;

    return PLATFORM_UNKNOWN;
}

const char *platform_name(Platform p)
{
    if ((int)p < 0 || (int)p >= PLATFORM_COUNT)
        return g_platforms[0].name;

    return g_platforms[p].name;
}

void platform_brand_color(Platform p, double *r, double *g, double *b)
{
    if ((int)p < 0 || (int)p >= PLATFORM_COUNT)
        p = PLATFORM_UNKNOWN;

    *r = g_platforms[p].r;
    *g = g_platforms[p].g;
    *b = g_platforms[p].b;
}

typedef struct {
    cairo_surface_t *surface;
    int              px;
} IconCache;

static IconCache g_icons[PLATFORM_COUNT];

static cairo_surface_t *icon_surface(Platform p, int px)
{
    g_autofree gchar *path = NULL;
    g_autoptr(GError) error = NULL;
    RsvgHandle       *handle;
    cairo_surface_t  *surface;
    RsvgRectangle     viewport;
    cairo_t          *cr;

    if ((int)p < 0 || (int)p >= PLATFORM_COUNT || g_platforms[p].slug == NULL)
        return NULL;

    if (px < 8)
        px = 8;

    if (g_icons[p].surface != NULL && g_icons[p].px == px)
        return g_icons[p].surface;

    if (g_icons[p].surface != NULL) {
        cairo_surface_destroy(g_icons[p].surface);
        g_icons[p].surface = NULL;
    }

    {
        g_autofree gchar *file = g_strdup_printf("%s.svg", g_platforms[p].slug);

        path = g_build_filename("src", "icons", file, NULL);
    }

    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_warning("icon: %s not found", path);
        return NULL;
    }

    handle = rsvg_handle_new_from_file(path, &error);
    if (handle == NULL) {
        g_warning("icon: cannot parse %s: %s", path, error->message);
        return NULL;
    }

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        return NULL;
    }

    cr = cairo_create(surface);

    /* 使用 SVG alpha 作为白色遮罩。 */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    viewport.x = 0.0;
    viewport.y = 0.0;
    viewport.width  = (double)px;
    viewport.height = (double)px;

    if (!rsvg_handle_render_document(handle, cr, &viewport, &error)) {
        g_warning("icon: render failed %s: %s", path, error->message);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        return NULL;
    }

    cairo_destroy(cr);
    g_object_unref(handle);

    g_icons[p].surface = surface;
    g_icons[p].px      = px;

    return surface;
}

typedef struct {
    cairo_surface_t *surface;   /* 官方字标：白色 alpha 遮罩。 */
    double           aspect;
    gboolean         tried;
} WordCache;

#define WRD_PX 128

static WordCache g_words[PLATFORM_COUNT];

static gchar *wordmark_path(Platform p)
{
    g_autofree gchar *name = g_strdup_printf("wordmark-%s.svg", g_platforms[p].slug);

    return g_build_filename("src", "icons", name, NULL);
}

/* 从 width/height 或 viewBox 读取宽高比。 */
static double wordmark_aspect(RsvgHandle *handle)
{
    gboolean      has_w = FALSE, has_h = FALSE, has_vb = FALSE;
    RsvgLength    w = { 0 }, h = { 0 };
    RsvgRectangle vb = { 0, 0, 0, 0 };
    double        iw = 0.0, ih = 0.0;
    double        aspect = 0.0;

    if (rsvg_handle_get_intrinsic_size_in_pixels(handle, &iw, &ih) && iw > 0 && ih > 0)
        return iw / ih;

    rsvg_handle_get_intrinsic_dimensions(handle, &has_w, &w, &has_h, &h, &has_vb, &vb);

    if (has_vb && vb.width > 0 && vb.height > 0)
        aspect = vb.width / vb.height;
    else if (has_w && has_h && w.length > 0 && h.length > 0)
        aspect = w.length / h.length;

    return aspect > 0.0 ? aspect : 0.0;
}

static WordCache *wordmark_cache(Platform p)
{
    WordCache         *cache;
    g_autofree gchar  *path = NULL;
    g_autoptr(GError)  error = NULL;
    RsvgHandle        *handle;
    cairo_surface_t   *surface;
    RsvgRectangle      viewport;
    cairo_t           *cr;
    double             aspect;
    int                px_w;

    if ((int)p <= 0 || (int)p >= PLATFORM_COUNT)
        return NULL;

    cache = &g_words[p];

    if (cache->tried)
        return cache->surface != NULL ? cache : NULL;

    cache->tried = TRUE;

    if (g_platforms[p].slug == NULL)
        return NULL;

    path = wordmark_path(p);

    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        return NULL;

    handle = rsvg_handle_new_from_file(path, &error);
    if (handle == NULL) {
        g_warning("wordmark: cannot parse %s: %s", path, error->message);
        return NULL;
    }

    aspect = wordmark_aspect(handle);

    if (aspect <= 0.0) {
        g_warning("wordmark: %s has no usable size", path);
        g_object_unref(handle);
        return NULL;
    }

    px_w   = MAX(8, (int)lround((double)WRD_PX * aspect));
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px_w, WRD_PX);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        return NULL;
    }

    cr = cairo_create(surface);
    viewport.x      = 0.0;
    viewport.y      = 0.0;
    viewport.width  = (double)px_w;
    viewport.height = (double)WRD_PX;

    if (!rsvg_handle_render_document(handle, cr, &viewport, &error)) {
        g_warning("wordmark: render failed %s: %s", path, error->message);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        return NULL;
    }

    cairo_destroy(cr);
    g_object_unref(handle);

    cache->surface = surface;
    cache->aspect  = aspect;

    return cache;
}

gboolean platform_wordmark_size(Platform p, double max_h, double max_w,
                                double *out_w, double *out_h)
{
    WordCache *cache = wordmark_cache(p);
    double     w, h;

    if (cache == NULL || max_h <= 0.0 || max_w <= 0.0)
        return FALSE;

    h = max_h;
    w = h * cache->aspect;

    if (w > max_w) {
        w = max_w;
        h = w / cache->aspect;
    }

    if (out_w != NULL) *out_w = w;
    if (out_h != NULL) *out_h = h;

    return TRUE;
}

void platform_draw_wordmark(cairo_t *cr, Platform p, double x, double y,
                            double w, double h)
{
    WordCache       *cache = wordmark_cache(p);
    cairo_surface_t *surface;
    double           sw, sh;

    if (cache == NULL || w <= 0.0 || h <= 0.0)
        return;

    surface = cache->surface;
    sw      = (double)cairo_image_surface_get_width(surface);
    sh      = (double)cairo_image_surface_get_height(surface);

    if (sw <= 0.0 || sh <= 0.0)
        return;

    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, w / sw, h / sh);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
    cairo_mask_surface(cr, surface, 0.0, 0.0);
    cairo_restore(cr);
}

static void icon_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    double r = MIN(radius, MIN(w, h) * 0.5);

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

void platform_draw_icon(cairo_t *cr, Platform p, double x, double y, double size)
{
    double           r, g, b;
    double           inset;
    cairo_surface_t *icon;

    platform_brand_color(p, &r, &g, &b);

    cairo_set_source_rgb(cr, r, g, b);
    icon_rounded_rect(cr, x, y, size, size, size * 0.24);
    cairo_fill(cr);

    inset = size * 0.20;
    icon  = icon_surface(p, (int)lround((size - inset * 2.0) * 2.0));

    if (icon == NULL)
        return;

    cairo_save(cr);
    cairo_translate(cr, x + inset, y + inset);
    cairo_scale(cr, (size - inset * 2.0) / (double)g_icons[p].px,
                     (size - inset * 2.0) / (double)g_icons[p].px);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.96);
    cairo_mask_surface(cr, icon, 0.0, 0.0);

    cairo_restore(cr);
}
