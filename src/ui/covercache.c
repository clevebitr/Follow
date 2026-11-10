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

#include "covercache.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>

#include "cache.h"
#include "config.h"
#include "cover.h"
#include "http.h"
#include "texture.h"
#include "ui.h"

/* 列表页最大的中心封面约 440px；512 够用，也不至于一张图就吃掉几 MB */
#define COVERCACHE_MAX_DIM 640

typedef struct {
    cairo_surface_t *surface;
    cairo_surface_t *scaled;
    int              scaled_px;
    gint64           ready_us;
    Palette          palette;
    gboolean         has_palette;
    gboolean         attempted;   /* 试过了：成功或失败都不再自动重试 */
    gboolean         loading;
} CoverEntry;

typedef struct {
    guint   gen;
    gsize   index;
    gchar  *url;
    gchar  *cache_path;
    GBytes *bytes;
    gchar  *error;
} CoverJob;

static CoverEntry *g_entries = NULL;
static gsize       g_count   = 0;
static guint       g_gen     = 0;
static guint       g_cover_gen = 0;

static void entry_clear(CoverEntry *e)
{
    g_clear_pointer(&e->surface, cairo_surface_destroy);
    g_clear_pointer(&e->scaled, cairo_surface_destroy);
    e->scaled_px = 0;
    e->ready_us  = 0;
    palette_reset(&e->palette);
    e->has_palette = FALSE;
    e->attempted   = FALSE;
    e->loading     = FALSE;
}

void covercache_clear(void)
{
    if (g_entries != NULL) {
        for (gsize i = 0; i < g_count; i++)
            entry_clear(&g_entries[i]);
    }

    g_clear_pointer(&g_entries, g_free);
    g_count = 0;
    g_gen++;
}

void covercache_sync(void)
{
    gsize n = config_get()->n_tracks;

    if (n == g_count && (n == 0 || g_entries != NULL))
        return;

    covercache_clear();

    if (n > 0) {
        g_entries = g_new0(CoverEntry, n);
        g_count   = n;
    }
}

static gboolean decode_pixbuf(GdkPixbuf *pixbuf, CoverEntry *entry)
{
    const int     w      = gdk_pixbuf_get_width(pixbuf);
    const int     h      = gdk_pixbuf_get_height(pixbuf);
    const int     nch    = gdk_pixbuf_get_n_channels(pixbuf);
    const int     stride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar *px     = gdk_pixbuf_get_pixels(pixbuf);
    guchar       *rgba;

    if (w <= 0 || h <= 0 || nch < 3)
        return FALSE;

    rgba = g_malloc_n((gsize)w * (gsize)h, 4);

    for (int y = 0; y < h; y++) {
        const guchar *src = px + (gsize)y * (gsize)stride;
        guchar       *dst = rgba + (gsize)y * (gsize)w * 4;

        for (int x = 0; x < w; x++) {
            dst[x * 4 + 0] = src[x * nch + 0];
            dst[x * 4 + 1] = src[x * nch + 1];
            dst[x * 4 + 2] = src[x * nch + 2];
            dst[x * 4 + 3] = nch >= 4 ? src[x * nch + 3] : 255;
        }
    }

    palette_from_rgba(rgba, w, h, &entry->palette);
    entry->has_palette = entry->palette.has;

    entry->surface = rgba_to_cairo_surface_fit(rgba, w, h, COVERCACHE_MAX_DIM);
    g_free(rgba);

    if (entry->surface != NULL) {
        entry->ready_us = g_get_monotonic_time();
        g_cover_gen++;
        return TRUE;
    }

    return FALSE;
}

static void load_local(gsize index, const AudioTrack *track, CoverEntry *entry)
{
    g_autofree gchar *path = config_to_local_path(track->pic);
    GdkPixbuf        *pixbuf = path != NULL ? gdk_pixbuf_new_from_file(path, NULL) : NULL;

    (void)index;

    entry->attempted = TRUE;

    if (pixbuf == NULL) {
        g_warning("covercache: cannot load %s", track->pic);
        return;
    }

    decode_pixbuf(pixbuf, entry);
    g_object_unref(pixbuf);
}

static gboolean covercache_done(gpointer data)
{
    CoverJob   *job   = data;
    CoverEntry *entry = NULL;

    if (job->gen == g_gen && job->index < g_count)
        entry = &g_entries[job->index];

    if (entry != NULL) {
        entry->loading   = FALSE;
        entry->attempted = TRUE;

        if (job->bytes != NULL) {
            g_autoptr(GError) error  = NULL;
            GInputStream     *stream = g_memory_input_stream_new_from_bytes(job->bytes);
            GdkPixbuf        *pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);

            if (pixbuf != NULL) {
                decode_pixbuf(pixbuf, entry);
                g_object_unref(pixbuf);
                cache_prune_prefix("cover", config_cover_max_bytes());
            } else {
                g_warning("covercache: cannot decode %s: %s", job->url,
                          error != NULL ? error->message : "unknown error");
            }

            g_object_unref(stream);
        } else {
            g_warning("covercache: download failed %s: %s", job->url,
                      job->error != NULL ? job->error : "unknown error");
        }

        if (app.canvas != NULL) {
            ui_request_redraw();
            ui_animate();
            gtk_widget_queue_draw(app.canvas);
        }
    }

    g_clear_pointer(&job->bytes, g_bytes_unref);
    g_free(job->url);
    g_free(job->cache_path);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer covercache_thread(gpointer data)
{
    CoverJob     *job  = data;
    HttpResponse  resp = { 0 };

    if (http_get(job->url, &resp, 20) == 0 && resp.status == 200) {
        job->bytes = g_bytes_new(resp.body, resp.length);

        if (job->cache_path != NULL) {
            gsize         size = 0;
            gconstpointer body = g_bytes_get_data(job->bytes, &size);

            g_file_set_contents(job->cache_path, body, (gssize)size, NULL);
        }
    } else if (resp.error[0] != 0) {
        job->error = g_strdup(resp.error);
    } else {
        job->error = g_strdup_printf("HTTP %ld", resp.status);
    }

    http_response_free(&resp);

    g_idle_add(covercache_done, job);
    return NULL;
}

static void load_remote(gsize index, const AudioTrack *track, CoverEntry *entry)
{
    g_autofree gchar *cache_path = follow_cache_file("cover", track->pic, "img");

    if (cache_path != NULL && g_file_test(cache_path, G_FILE_TEST_EXISTS)) {
        GdkPixbuf *cached = gdk_pixbuf_new_from_file(cache_path, NULL);

        if (cached != NULL) {
            entry->attempted = TRUE;
            decode_pixbuf(cached, entry);
            g_object_unref(cached);
            return;
        }

        /* 缓存文件坏了，删掉重下一次 */
        g_unlink(cache_path);
    }

    {
        CoverJob *job = g_new0(CoverJob, 1);

        job->gen        = g_gen;
        job->index      = index;
        job->url        = cover_url_normalize(track->pic);
        job->cache_path = g_steal_pointer(&cache_path);

        if (job->url == NULL || *job->url == '\0') {
            entry->attempted = TRUE;
            g_free(job->url);
            g_free(job->cache_path);
            g_free(job);
            return;
        }

        entry->loading = TRUE;
        g_thread_unref(g_thread_new("follow-covercache", covercache_thread, job));
    }
}

cairo_surface_t *covercache_surface(gsize index)
{
    const APPconfig  *cfg = config_get();
    const AudioTrack *track;
    CoverEntry       *entry;

    if (index >= g_count || g_entries == NULL)
        return NULL;

    entry = &g_entries[index];

    if (entry->surface != NULL || entry->attempted || entry->loading)
        return entry->surface;

    track = config_track_at(cfg, index);

    if (track == NULL || track->pic == NULL || *track->pic == '\0') {
        entry->attempted = TRUE;
        return NULL;
    }

    if (track->pic_remote)
        load_remote(index, track, entry);
    else
        load_local(index, track, entry);

    return entry->surface;
}

const Palette *covercache_palette(gsize index)
{
    if (index >= g_count || g_entries == NULL)
        return NULL;

    return g_entries[index].has_palette ? &g_entries[index].palette : NULL;
}

double covercache_reveal(gsize index)
{
    double age, t;

    if (!config_animations())
        return 1.0;

    if (index >= g_count || g_entries == NULL ||
        g_entries[index].surface == NULL || g_entries[index].ready_us <= 0)
        return 0.0;

    age = (double)(g_get_monotonic_time() - g_entries[index].ready_us) / 1000000.0;
    t   = CLAMP(age / 0.32, 0.0, 1.0);

    return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
}

cairo_surface_t *covercache_scaled(gsize index, int px)
{
    CoverEntry      *entry;
    cairo_surface_t *surface;
    cairo_t         *cr;
    double           sw, sh, scale, dw, dh;

    if (index >= g_count || g_entries == NULL)
        return NULL;

    entry = &g_entries[index];

    if (entry->surface == NULL)
        return NULL;

    px = MAX(16, (px + 7) & ~7);

    if (entry->scaled != NULL && entry->scaled_px == px)
        return entry->scaled;

    g_clear_pointer(&entry->scaled, cairo_surface_destroy);

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    sw = (double)cairo_image_surface_get_width(entry->surface);
    sh = (double)cairo_image_surface_get_height(entry->surface);

    if (sw <= 0.0 || sh <= 0.0) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    scale = MAX((double)px / sw, (double)px / sh);
    dw    = sw * scale;
    dh    = sh * scale;

    cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_translate(cr, ((double)px - dw) * 0.5, ((double)px - dh) * 0.5);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, entry->surface, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);

    entry->scaled    = surface;
    entry->scaled_px = px;

    return entry->scaled;
}

gint64 covercache_version(gsize index)
{
    if (index >= g_count || g_entries == NULL)
        return 0;

    return g_entries[index].ready_us;
}

gboolean covercache_animating(void)
{
    if (!config_animations() || g_entries == NULL)
        return FALSE;

    for (gsize i = 0; i < g_count; i++) {
        if (g_entries[i].surface != NULL &&
            covercache_reveal(i) < 0.999)
            return TRUE;
    }

    return FALSE;
}
