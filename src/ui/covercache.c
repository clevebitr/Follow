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
#include "playlist.h"
#include "texture.h"
#include "ui.h"

/* 列表页最大的中心封面约 440px；512 够用，也不至于一张图就吃掉几 MB */
#define COVERCACHE_MAX_DIM 640

typedef struct {
    cairo_surface_t *surface;
    gint64           ready_us;
    Palette          palette;
    gboolean         has_palette;
    gboolean         attempted;   /* 试过了：成功或失败都不再自动重试 */
    gboolean         loading;
} CoverEntry;

typedef struct {
    guint            gen;
    gsize            index;
    gchar           *path;        /* 本地图片 / 远程磁盘缓存 */
    gchar           *url;         /* 远程地址，本地图为 NULL */
    gchar           *cache_path;  /* 远程下载后写到这 */
    cairo_surface_t *surface;
    Palette          palette;
    gboolean         has_palette;
    gboolean         downloaded;
    gchar           *error;
} CoverJob;

static CoverEntry *g_entries = NULL;
static gsize       g_count   = 0;
static guint       g_gen     = 0;
static guint       g_cover_gen = 0;

static void entry_clear(CoverEntry *e)
{
    g_clear_pointer(&e->surface, cairo_surface_destroy);
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

/* 解码 + 取色 + 缩放到缓存尺寸。纯 CPU / cairo 操作，可以在后台线程跑。 */
static gboolean decode_pixbuf(GdkPixbuf *pixbuf, cairo_surface_t **out_surface,
                              Palette *out_palette, gboolean *out_has_palette)
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

    palette_from_rgba(rgba, w, h, out_palette);
    *out_has_palette = out_palette->has;
    *out_surface = rgba_to_cairo_surface_fit(rgba, w, h, COVERCACHE_MAX_DIM);
    g_free(rgba);

    return *out_surface != NULL;
}

static gboolean covercache_done(gpointer data);

static gpointer covercache_thread(gpointer data)
{
    CoverJob *job = data;
    GdkPixbuf *pixbuf = NULL;

    /* 1) 本地文件或远程磁盘缓存 */
    if (job->path != NULL && g_file_test(job->path, G_FILE_TEST_EXISTS)) {
        pixbuf = gdk_pixbuf_new_from_file(job->path, NULL);

        if (pixbuf == NULL)
            g_unlink(job->path);   /* 缓存坏了，删掉重下 */
    }

    /* 2) 远程下载 */
    if (pixbuf == NULL && job->url != NULL && *job->url != '\0') {
        HttpResponse resp = { 0 };

        if (http_get(job->url, &resp, 20) == 0 && resp.status == 200) {
            if (job->cache_path != NULL) {
                g_file_set_contents(job->cache_path, resp.body, resp.length, NULL);
                job->downloaded = TRUE;
            }

            {
                GInputStream *stream = g_memory_input_stream_new_from_data(
                    resp.body, resp.length, NULL);
                GError *error = NULL;

                pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
                g_object_unref(stream);

                if (pixbuf == NULL)
                    job->error = g_strdup(error != NULL ? error->message
                                                       : "cannot decode image");
                g_clear_error(&error);
            }
        } else if (resp.error[0] != 0) {
            job->error = g_strdup(resp.error);
        } else {
            job->error = g_strdup_printf("HTTP %ld", resp.status);
        }

        http_response_free(&resp);
    }

    if (pixbuf != NULL) {
        if (!decode_pixbuf(pixbuf, &job->surface, &job->palette,
                           &job->has_palette) && job->error == NULL)
            job->error = g_strdup("cannot decode image");
        g_object_unref(pixbuf);
    }

    if (job->surface == NULL && job->error == NULL)
        job->error = g_strdup("no cover file");

    g_idle_add(covercache_done, job);
    return NULL;
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

        if (job->surface != NULL) {
            g_clear_pointer(&entry->surface, cairo_surface_destroy);
            entry->surface     = job->surface;
            job->surface       = NULL;
            entry->palette     = job->palette;
            entry->has_palette = job->has_palette;
            entry->ready_us    = g_get_monotonic_time();
            g_cover_gen++;

            if (job->downloaded)
                cache_prune_prefix("cover", config_cover_max_bytes());
        } else {
            g_warning("covercache: %s: %s", job->path != NULL ? job->path : job->url,
                      job->error != NULL ? job->error : "unknown error");
        }

        playlist_request_frame();
        ui_animate();
    }

    g_clear_pointer(&job->surface, cairo_surface_destroy);
    g_free(job->path);
    g_free(job->url);
    g_free(job->cache_path);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static void load_local(gsize index, const AudioTrack *track, CoverEntry *entry)
{
    CoverJob *job = g_new0(CoverJob, 1);

    job->gen   = g_gen;
    job->index = index;
    job->path  = config_to_local_path(track->pic);

    entry->attempted = TRUE;

    if (job->path == NULL) {
        g_warning("covercache: cannot resolve %s", track->pic);
        g_free(job);
        return;
    }

    entry->loading = TRUE;
    g_thread_unref(g_thread_new("follow-covercache", covercache_thread, job));
}

static void load_remote(gsize index, const AudioTrack *track, CoverEntry *entry)
{
    CoverJob *job = g_new0(CoverJob, 1);

    job->gen        = g_gen;
    job->index      = index;
    job->path       = follow_cache_file("cover", track->pic, "img");
    job->cache_path = g_strdup(job->path);
    job->url        = cover_url_normalize(track->pic);

    entry->attempted = TRUE;

    if ((job->path == NULL || *job->path == '\0') &&
        (job->url == NULL || *job->url == '\0')) {
        g_free(job->path);
        g_free(job->url);
        g_free(job->cache_path);
        g_free(job);
        return;
    }

    entry->loading = TRUE;
    g_thread_unref(g_thread_new("follow-covercache", covercache_thread, job));
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
