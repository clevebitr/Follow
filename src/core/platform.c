#include <math.h>
#include <string.h>

#include <librsvg/rsvg.h>

#include "platform.h"

/* 图标来自 Simple Icons（https://simpleicons.org，CC0-1.0 公共领域）：
 * 纯 SVG path，运行时用 librsvg 渲染成 surface，再用 alpha 当遮罩涂成白色。
 * 想换图标/加平台，只要往 src/icons/ 里丢一个同名 SVG。 */

static const struct {
    const char *id;
    const char *name;
    const char *slug;          /* src/icons/<slug>.svg */
    double      r, g, b;
    const char *hosts[5];
} g_platforms[] = {
    /* PLATFORM_UNKNOWN */
    { "link", "LINK", NULL, 0.35, 0.35, 0.38, { NULL } },

    /* PLATFORM_BILIBILI */
    { "bilibili", "BILIBILI", "bilibili", 0.984, 0.447, 0.600,
      { "bilibili.com", "b23.tv", "bili2233.cn", "bilibili.tv", NULL } },

    /* PLATFORM_APPLE_MUSIC */
    { "applemusic", "APPLE MUSIC", "applemusic", 0.980, 0.176, 0.282,
      { "music.apple.com", "itunes.apple.com", "apple.co", NULL, NULL } },

    /* PLATFORM_YOUTUBE */
    { "youtube", "YOUTUBE", "youtube", 1.000, 0.000, 0.000,
      { "youtube.com", "youtu.be", "music.youtube.com", NULL, NULL } },

    /* PLATFORM_SPOTIFY */
    { "spotify", "SPOTIFY", "spotify", 0.114, 0.725, 0.329,
      { "spotify.com", "spoti.fi", "open.spotify.com", NULL, NULL } },

    /* PLATFORM_SOUNDCLOUD */
    { "soundcloud", "SOUNDCLOUD", "soundcloud", 1.000, 0.333, 0.000,
      { "soundcloud.com", "snd.sc", NULL, NULL, NULL } },

    /* PLATFORM_NETEASE */
    { "netease", "NETEASE", "neteasecloudmusic", 0.760, 0.047, 0.047,
      { "music.163.com", "163cn.tv", "y.music.163.com", NULL, NULL } },

    /* PLATFORM_BANDCAMP */
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

    /* 常见别名 */
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

const char *platform_id(Platform p)
{
    if ((int)p < 0 || (int)p >= PLATFORM_COUNT)
        return g_platforms[0].id;

    return g_platforms[p].id;
}

void platform_brand_color(Platform p, double *r, double *g, double *b)
{
    if ((int)p < 0 || (int)p >= PLATFORM_COUNT)
        p = PLATFORM_UNKNOWN;

    *r = g_platforms[p].r;
    *g = g_platforms[p].g;
    *b = g_platforms[p].b;
}

/* ---------- 图标渲染（librsvg + 缓存） ---------- */

typedef struct {
    cairo_surface_t *surface;
    int              px;
} IconCache;

static IconCache g_icons[PLATFORM_COUNT];

/* 把 SVG 渲染成 px×px、透明底、纯白的图标 surface */
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

    /* SVG 默认是黑字透明底；我们只用它的 alpha 当遮罩 */
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

    /* 品牌色底板 */
    cairo_set_source_rgb(cr, r, g, b);
    icon_rounded_rect(cr, x, y, size, size, size * 0.24);
    cairo_fill(cr);

    /* 白色图标：用 SVG 的 alpha 作遮罩，任意尺寸都清晰 */
    inset = size * 0.20;
    icon  = icon_surface(p, (int)lround((size - inset * 2.0) * 2.0));   /* 2x 超采样 */

    if (icon == NULL)
        return;      /* 没图标就只留底板，不要退回手绘 */

    cairo_save(cr);
    cairo_translate(cr, x + inset, y + inset);
    cairo_scale(cr, (size - inset * 2.0) / (double)g_icons[p].px,
                     (size - inset * 2.0) / (double)g_icons[p].px);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.96);
    cairo_mask_surface(cr, icon, 0.0, 0.0);

    cairo_restore(cr);
}
