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
#ifndef FOLLOW_UI_H
#define FOLLOW_UI_H

#include <gtk/gtk.h>
#include <qrencode.h>

#include "config.h"
#include "fx.h"
#include "lyrics.h"
#include "media.h"
#include "palette.h"
#include "platform.h"
#include "stats.h"

typedef struct {
    gchar           *text;
    gchar           *label;
    Platform         platform;
    gboolean         with_icon;
    QRcode          *code;
    cairo_surface_t *surface;
    int              pixels;
} QrSlot;

typedef struct {
    GtkWidget *window;
    GtkWidget *header_bar;
    GtkWidget *header_title;
    GtkWidget *canvas;
    GtkWidget *lyricgl;
    GtkWidget *wallgl;
    GtkWidget *chrome;
    GtkWidget *perf;
    GtkWidget *bg_picture;
    GtkWidget *bg_gl;
    guint      tick_id;
    guint      bg_tick_id;

    gboolean dirty;
    gboolean win_dragging;
    gboolean chrome_dirty;
    gboolean help;
    gboolean last_playing;
    gboolean last_paused;
    gint     last_width;
    gint     last_height;
    gint     shown_second;
    gint64   shown_wall_second;

    Media    media;
    gint     track;
    gint     queue_next;
    gint     repeat;
    gboolean shuffle;
    gint     history[64];
    gint     history_len;
    guint    resolve_gen;
    gboolean source_pending;
    gboolean source_cached;
    gboolean source_retried;
    gchar   *resolved_title;
    gchar   *resolved_artist;
    gchar   *source_error;

    Lyrics  *lyrics;
    gchar   *lyric_line;
    gchar   *lyric_next;
    gssize   lyric_index;
    gchar   *lyric_prev;
    Fx       fx_lyric;
    gchar   *lyric_error;
    gchar   *lyrics_spec;
    guint    lyrics_gen;
    gboolean lyrics_pending;
    gboolean lyrics_fit_checked;
    gint     lrc_offset_ms;
    gint     lrc_offset_base;

    cairo_surface_t *cover;
    cairo_surface_t *cover_scaled;
    int              cover_scaled_px;
    guint            cover_gen;
    Palette          palette;

    cairo_surface_t *bg_base;
    cairo_surface_t *bg_prev;
    Palette          pal_bg_prev;
    cairo_surface_t *bg_frame;
    int              bg_base_w;
    int              bg_base_h;
    double           bg_phase;

    QrSlot qr[2];

    Fx     fx_bg;
    Fx     fx_stage;
    guint  anim_id;
    cairo_surface_t *snapshot;

    Stats  stats;
    gint64 stats_last_us;
    gint64 stats_saved_us;
    double stats_fraction;
    gint   stats_dirty_seconds;
} App;

extern App app;

static inline void ui_request_redraw(void)
{
    app.dirty = TRUE;
}

void ui_animate(void);

/* 根据墙 / GL 状态切换图层可见性。 */
void ui_sync_layers(void);

void ui_reload_config(void);
void ui_toggle_fullscreen(void);

/* 打开配置文件；成功返回 NULL，失败返回错误文本。 */
gchar *ui_open_config_file(void);

#endif
