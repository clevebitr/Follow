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

#include "cover.h"

#include <math.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "backdrop.h"
#include "cache.h"
#include "http.h"
#include "metadata.h"
#include "texture.h"
#include "ui.h"

#define COVER_MAX_DIM 768

typedef struct {
    guint   gen;
    gchar  *url;
    gchar  *cache_path;
    GBytes *bytes;
    gchar  *error;
} CoverJob;

/* 图床 @ 后缀改为正方形 JPEG；.avif 改为 .jpg。 */
gchar *cover_url_normalize(const char *url)
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

/* 安装封面：取色并缩放到 768px。 */
static void install_pixbuf(GdkPixbuf *pixbuf)
{
    const int     w      = gdk_pixbuf_get_width(pixbuf);
    const int     h      = gdk_pixbuf_get_height(pixbuf);
    const int     nch    = gdk_pixbuf_get_n_channels(pixbuf);
    const int     stride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar *px     = gdk_pixbuf_get_pixels(pixbuf);
    guchar       *rgba;

    if (w <= 0 || h <= 0 || nch < 3)
        return;

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

    palette_from_rgba(rgba, w, h, &app.palette);

    cover_clear();
    app.cover = rgba_to_cairo_surface_fit(rgba, w, h, COVER_MAX_DIM);
    g_free(rgba);

    backdrop_invalidate();
    ui_request_redraw();
}

static gboolean cover_done(gpointer data)
{
    CoverJob *job = data;

    if (job->gen == app.cover_gen) {
        if (job->bytes != NULL) {
            g_autoptr(GError)  error  = NULL;
            GInputStream      *stream = g_memory_input_stream_new_from_bytes(job->bytes);
            GdkPixbuf         *pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);

            if (pixbuf != NULL) {
                install_pixbuf(pixbuf);
                g_object_unref(pixbuf);

                if (job->cache_path != NULL) {
                    gsize           size = 0;
                    gconstpointer   data = g_bytes_get_data(job->bytes, &size);

                    g_file_set_contents(job->cache_path, data, (gssize)size, NULL);
                    cache_prune_prefix("cover", config_cover_max_bytes());
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

    g_clear_pointer(&job->bytes, g_bytes_unref);
    g_free(job->url);
    g_free(job->cache_path);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer cover_thread(gpointer data)
{
    CoverJob     *job  = data;
    HttpResponse  resp = { 0 };

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

static void cover_set_remote(const AudioTrack *track)
{
    g_autofree gchar *cache_path = follow_cache_file("cover", track->pic, "img");

    if (cache_path != NULL && g_file_test(cache_path, G_FILE_TEST_EXISTS)) {
        GdkPixbuf *cached = gdk_pixbuf_new_from_file(cache_path, NULL);

        if (cached != NULL) {
            install_pixbuf(cached);
            g_object_unref(cached);
            g_message("cover: cache hit %s", cache_path);
            return;
        }
    }

    {
        CoverJob *job = g_new0(CoverJob, 1);

        job->gen        = app.cover_gen;
        job->url        = cover_url_normalize(track->pic);
        job->cache_path = g_steal_pointer(&cache_path);

        g_message("cover: fetching %s", job->url);
        g_thread_unref(g_thread_new("follow-cover", cover_thread, job));
    }
}

void cover_clear(void)
{
    g_clear_pointer(&app.cover, cairo_surface_destroy);
    g_clear_pointer(&app.cover_scaled, cairo_surface_destroy);
    app.cover_scaled_px = 0;
}

void cover_set(const AudioTrack *track)
{
    g_autoptr(GError) error = NULL;
    GdkPixbuf        *pixbuf;
    gchar            *path;

    backdrop_swap();

    cover_clear();
    app.cover_gen++;
    palette_reset(&app.palette);
    ui_request_redraw();

    if (track == NULL || track->pic == NULL || *track->pic == '\0')
        return;

    if (track->pic_remote) {
        cover_set_remote(track);
        return;
    }

    path   = config_to_local_path(track->pic);
    pixbuf = path != NULL ? gdk_pixbuf_new_from_file(path, &error) : NULL;

    if (pixbuf == NULL) {
        g_warning("cover: cannot load %s: %s", track->pic,
                  error != NULL ? error->message : "unknown error");
        g_free(path);
        return;
    }

    install_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    g_free(path);
}

void cover_apply_metadata(void)
{
    const APPconfig  *cfg   = config_get();
    gint              index = app.track >= 0 ? app.track : 0;
    const AudioTrack *track = config_track_at(cfg, (gsize)index);
    const gchar      *path;
    GdkPixbuf        *pixbuf;

    if (!config_metadata_cover())
        return;

    /* 配置 pic 优先。 */
    if (track != NULL && track->pic != NULL && *track->pic != '\0')
        return;

    if (app.cover != NULL)
        return;

    /* 切换元数据到当前曲目；本地命中时同步可用。 */
    {
        g_autofree gchar *key = metadata_track_key(track);

        if (metadata_state() != META_STATE_READY ||
            g_strcmp0(key, metadata_current_key()) != 0) {
            if (track != NULL)
                metadata_load_track(track);

            if (metadata_state() != META_STATE_READY ||
                g_strcmp0(key, metadata_current_key()) != 0)
                return;
        }
    }

    path = metadata_cover_path();
    if (path == NULL || *path == '\0' || !g_file_test(path, G_FILE_TEST_EXISTS))
        return;

    pixbuf = gdk_pixbuf_new_from_file(path, NULL);
    if (pixbuf == NULL) {
        g_warning("cover: cannot load metadata cover %s", path);
        return;
    }

    install_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    g_message("cover: metadata cover %s", path);
}

cairo_surface_t *cover_base(void)
{
    return app.cover;
}

static cairo_surface_t *cover_scaled_for(double size)
{
    const double sw = (double)cairo_image_surface_get_width(app.cover);
    const double sh = (double)cairo_image_surface_get_height(app.cover);
    int          target, dw, dh;
    cairo_t     *cr;

    if (app.cover == NULL || sw <= 0.0 || sh <= 0.0)
        return NULL;

    target = ((int)size / 4) * 4;
    if (target < 4)
        target = 4;

    if (app.cover_scaled != NULL && app.cover_scaled_px == target)
        return app.cover_scaled;

    cairo_surface_destroy(app.cover_scaled);

    dw = MAX(1, (int)(sw * ((double)target / MAX(sw, sh)) + 0.5));
    dh = MAX(1, (int)(sh * ((double)target / MAX(sw, sh)) + 0.5));

    app.cover_scaled    = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    app.cover_scaled_px = target;

    if (cairo_surface_status(app.cover_scaled) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(app.cover_scaled);
        app.cover_scaled    = NULL;
        app.cover_scaled_px = 0;
        return NULL;
    }

    cr = cairo_create(app.cover_scaled);
    cairo_scale(cr, (double)dw / sw, (double)dh / sh);
    cairo_set_source_surface(cr, app.cover, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_destroy(cr);

    return app.cover_scaled;
}

void cover_draw(cairo_t *cr, double x, double y, double size)
{
    cairo_surface_t *scaled;
    double           tw, th;

    if (size <= 1.0)
        return;

    if (app.cover == NULL) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.04);
        cairo_rectangle(cr, x, y, size, size);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x + 0.5, y + 0.5, size - 1.0, size - 1.0);
        cairo_stroke(cr);
        return;
    }

    scaled = cover_scaled_for(size);
    if (scaled == NULL)
        return;

    tw = (double)cairo_image_surface_get_width(scaled);
    th = (double)cairo_image_surface_get_height(scaled);

    cairo_set_source_surface(cr, scaled,
                             floor(x + (size - tw) * 0.5),
                             floor(y + (size - th) * 0.5));
    cairo_paint(cr);
}
