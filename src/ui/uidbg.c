/**
 * created: 2026/11/11
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#include "uidbg.h"

#include <gsk/gsk.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

#include "backdropgl.h"
#include "cmdbox.h"
#include "config.h"
#include "lyricgl.h"
#include "player.h"
#include "playlist.h"
#include "stage.h"
#include "ui.h"
#include "wallgl.h"

typedef struct {
    gchar *text;   /* 文字内容；衬底记成 tag */
    gchar *tag;
    double x, y, w, h;
    double px;
    Rgba   color;
} Item;

#define UIDBG_MAX_ITEMS  600      /* 每层最多记多少条，防止墙把内存吃掉 */
#define UIDBG_LOG_RING   400
#define UIDBG_FRAME_RING 120

typedef struct {
    gint     level;
    gchar   *domain;
    gchar   *message;
    gint64   at_us;
} LogRec;

static gboolean      g_on;
static gboolean      g_recording = TRUE;
static gint          g_layer = UIDBG_LAYER_STAGE;
static GPtrArray    *g_items[UIDBG_LAYER_MAX];
static GtkWidget    *g_window;
static GSocketService *g_service;
static guint          g_port = 7878;

static LogRec  g_logs[UIDBG_LOG_RING];
static guint   g_log_head;
static guint   g_log_count;
static GMutex  g_log_lock;
static GLogFunc g_log_prev;

static gint64  g_frame_start_us;
static gint64  g_frame_last_us;
static gint64  g_last_render_us;
static gint64  g_frame_gaps[UIDBG_FRAME_RING];
static guint   g_frame_gap_n;
static guint   g_frame_gap_i;

static const char *const g_layer_name[UIDBG_LAYER_MAX] = {
    "wall", "wall-chrome", "stage", "lyric", "palette"
};

static void item_free(Item *it)
{
    if (it == NULL)
        return;

    g_free(it->text);
    g_free(it->tag);
    g_free(it);
}

static GPtrArray *layer_items(int layer, gboolean create)
{
    if (layer < 0 || layer >= UIDBG_LAYER_MAX)
        return NULL;

    if (g_items[layer] == NULL && create)
        g_items[layer] = g_ptr_array_new_with_free_func((GDestroyNotify)item_free);

    return g_items[layer];
}

static void obj_set_str(JsonObject *o, const char *key, const char *value)
{
    json_object_set_string_member(o, key, value != NULL ? value : "");
}

static void obj_set_opt_str(JsonObject *o, const char *key, const char *value)
{
    if (value != NULL)
        json_object_set_string_member(o, key, value);
    else
        json_object_set_null_member(o, key);
}

/* #rrggbb */
static gchar *color_hex(Rgba c)
{
    return g_strdup_printf("#%02x%02x%02x",
                           (int)CLAMP(c.r * 255.0, 0.0, 255.0),
                           (int)CLAMP(c.g * 255.0, 0.0, 255.0),
                           (int)CLAMP(c.b * 255.0, 0.0, 255.0));
}

static double frame_avg_ms(void)
{
    gint64 sum = 0;

    if (g_frame_gap_n == 0)
        return 0.0;

    for (guint i = 0; i < g_frame_gap_n; i++)
        sum += g_frame_gaps[i];

    return (double)sum / (double)g_frame_gap_n / 1000.0;
}

static double frame_p95_ms(void)
{
    gint64 tmp[UIDBG_FRAME_RING];
    guint  n = g_frame_gap_n;

    if (n == 0)
        return 0.0;

    memcpy(tmp, g_frame_gaps, sizeof(gint64) * n);

    /* 插入排序：只有 120 个数，比 qsort 便宜 */
    for (guint i = 1; i < n; i++) {
        gint64 v = tmp[i];
        guint  j = i;

        while (j > 0 && tmp[j - 1] > v) {
            tmp[j] = tmp[j - 1];
            j--;
        }
        tmp[j] = v;
    }

    return (double)tmp[(guint)((double)(n - 1) * 0.95)] / 1000.0;
}

static double frame_fps(void)
{
    gint64 sum = 0;

    if (g_frame_gap_n < 2)
        return 0.0;

    for (guint i = 0; i < g_frame_gap_n; i++)
        sum += g_frame_gaps[i];

    /* 帧间隔的平均值换算成每秒多少帧 */
    return (double)g_frame_gap_n * 1000000.0 / (double)sum;
}

static void log_push(GLogLevelFlags level, const gchar *domain, const gchar *message)
{
    LogRec *rec;

    g_mutex_lock(&g_log_lock);

    rec = &g_logs[g_log_head];
    g_free(rec->domain);
    g_free(rec->message);

    rec->level   = (gint)(level & G_LOG_LEVEL_MASK);
    rec->domain  = g_strdup(domain);
    rec->message = g_strdup(message);
    rec->at_us   = g_get_monotonic_time();

    g_log_head = (g_log_head + 1) % UIDBG_LOG_RING;
    if (g_log_count < UIDBG_LOG_RING)
        g_log_count++;

    g_mutex_unlock(&g_log_lock);
}

static void uidbg_log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                              const gchar *message, gpointer user_data)
{
    (void)user_data;

    log_push(log_level, log_domain, message);

    if (g_log_prev != NULL)
        g_log_prev(log_domain, log_level, message, NULL);
    else
        g_log_default_handler(log_domain, log_level, message, NULL);
}

static const char *level_name(gint level)
{
    if ((level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL)) != 0)
        return "error";
    if ((level & G_LOG_LEVEL_WARNING) != 0)
        return "warning";
    if ((level & G_LOG_LEVEL_MESSAGE) != 0)
        return "message";
    if ((level & G_LOG_LEVEL_INFO) != 0)
        return "info";
    if ((level & G_LOG_LEVEL_DEBUG) != 0)
        return "debug";

    return "trace";
}

gboolean uidbg_enabled(void)
{
    return g_on;
}

void uidbg_suppress(gboolean on)
{
    g_recording = !on;
}

void uidbg_begin_layer(int layer)
{
    if (!g_on)
        return;

    g_layer = layer;
}

void uidbg_layer_clear(int layer)
{
    GPtrArray *items = layer_items(layer, FALSE);

    if (!g_on || items == NULL)
        return;

    g_ptr_array_set_size(items, 0);
}

void uidbg_record_text(double x, double y, double w, double h,
                       double px, Rgba color, const gchar *text)
{
    GPtrArray *items;
    Item      *it;

    if (!g_on || !g_recording || text == NULL || *text == '\0')
        return;

    if (h <= 0.0 || w <= 0.0)
        return;

    items = layer_items(g_layer, TRUE);
    if (items == NULL || items->len >= UIDBG_MAX_ITEMS)
        return;

    it = g_new0(Item, 1);
    it->text  = g_strdup(text);
    it->x     = x;
    it->y     = y;
    it->w     = w;
    it->h     = h;
    it->px    = px;
    it->color = color;

    g_ptr_array_add(items, it);
}

void uidbg_record_rect(double x, double y, double w, double h,
                       Rgba color, const gchar *tag)
{
    GPtrArray *items;
    Item      *it;

    if (!g_on || !g_recording || w <= 1.0 || h <= 1.0)
        return;

    items = layer_items(g_layer, TRUE);
    if (items == NULL || items->len >= UIDBG_MAX_ITEMS)
        return;

    it = g_new0(Item, 1);
    it->tag   = g_strdup(tag != NULL ? tag : "rect");
    it->x     = x;
    it->y     = y;
    it->w     = w;
    it->h     = h;
    it->color = color;

    g_ptr_array_add(items, it);
}

void uidbg_frame_begin(void)
{
    gint64 now;

    if (!g_on || !g_recording)
        return;

    now = g_get_monotonic_time();

    /* 只清 cairo 那一层（stage）：歌词层的记录由 lyricgl 自己的 render 负责
     * 清掉重建，它比 stage_render 晚一步，在这里清会被下一帧抹掉 */
    uidbg_layer_clear(UIDBG_LAYER_STAGE);

    g_frame_start_us = now;

    if (g_frame_last_us > 0) {
        g_frame_gaps[g_frame_gap_i] = now - g_frame_last_us;
        g_frame_gap_i = (g_frame_gap_i + 1) % UIDBG_FRAME_RING;
        if (g_frame_gap_n < UIDBG_FRAME_RING)
            g_frame_gap_n++;
    }

    g_frame_last_us = now;
}

void uidbg_frame_end(void)
{
    if (!g_on || !g_recording || g_frame_start_us == 0)
        return;

    g_last_render_us = g_get_monotonic_time() - g_frame_start_us;
    g_frame_start_us = 0;

    /* 一帧画完就回到主层：离屏 / 延迟渲染（磁贴、快照）不会把字记到上一帧的层上 */
    g_layer = UIDBG_LAYER_STAGE;
}

static void collect_widgets(GtkWidget *widget, GtkWidget *root, JsonArray *out,
                            guint *count, guint max)
{
    const gchar *text = NULL;
    graphene_rect_t bounds;

    if (widget == NULL || *count >= max)
        return;

    if (!gtk_widget_get_visible(widget))
        return;

    if (GTK_IS_LABEL(widget)) {
        text = gtk_label_get_text(GTK_LABEL(widget));
    } else if (GTK_IS_ENTRY(widget)) {
        text = gtk_editable_get_text(GTK_EDITABLE(widget));

        if (text == NULL || *text == '\0')
            text = gtk_entry_get_placeholder_text(GTK_ENTRY(widget));
    } else if (GTK_IS_BUTTON(widget)) {
        text = gtk_button_get_label(GTK_BUTTON(widget));
    }

    if (gtk_widget_get_visible(widget) && text != NULL && *text != '\0' &&
        gtk_widget_compute_bounds(widget, root, &bounds)) {
        JsonObject      *o = json_object_new();
        g_auto(GStrv)    css = gtk_widget_get_css_classes(widget);
        GString         *classes = g_string_new(NULL);

        for (gchar **c = css; c != NULL && *c != NULL; c++) {
            if (classes->len > 0)
                g_string_append_c(classes, ' ');
            g_string_append(classes, *c);
        }

        obj_set_str(o, "t", text);
        obj_set_str(o, "type", G_OBJECT_TYPE_NAME(widget));
        obj_set_str(o, "css", classes->str);
        json_object_set_double_member(o, "x", bounds.origin.x);
        json_object_set_double_member(o, "y", bounds.origin.y);
        json_object_set_double_member(o, "w", bounds.size.width);
        json_object_set_double_member(o, "h", bounds.size.height);
        json_object_set_boolean_member(o, "focused", gtk_widget_has_focus(widget));

        json_array_add_object_element(out, o);
        g_string_free(classes, TRUE);
        (*count)++;
    }

    for (GtkWidget *child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child))
        collect_widgets(child, root, out, count, max);
}

static cairo_status_t png_write_cb(void *closure, const unsigned char *data,
                                   unsigned int length)
{
    g_byte_array_append((GByteArray *)closure, data, length);
    return CAIRO_STATUS_SUCCESS;
}

static gboolean surface_is_blank(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);

    {
        unsigned char *data  = cairo_image_surface_get_data(surface);
        int            stride = cairo_image_surface_get_stride(surface);
        int            w      = cairo_image_surface_get_width(surface);
        int            h      = cairo_image_surface_get_height(surface);

        if (data == NULL)
            return TRUE;

        for (int y = 0; y < h; y += 4) {
            guint32 *row = (guint32 *)(data + (gsize)y * (gsize)stride);

            for (int x = 0; x < w; x += 4) {
                if ((row[x] >> 24) != 0)
                    return FALSE;
            }
        }
    }

    return TRUE;
}

static cairo_surface_t *render_window_surface(int width, int height)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                         width, height);
    cairo_t *cr = cairo_create(surface);

    if (g_window != NULL && gtk_widget_get_realized(g_window)) {
        GdkPaintable  *paintable = gtk_widget_paintable_new(g_window);
        GtkSnapshot   *snapshot  = gtk_snapshot_new();
        GskRenderNode *node;

        gdk_paintable_snapshot(paintable, snapshot, width, height);
        node = gtk_snapshot_to_node(snapshot);
        gsk_render_node_draw(node, cr);
        gsk_render_node_unref(node);

        g_object_unref(snapshot);
        g_object_unref(paintable);

        if (!surface_is_blank(surface)) {
            cairo_destroy(cr);
            return surface;
        }
    }

    /* 回退：只有主画布（cairo 层）。画的时候不要重复记录元素。 */
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr      = cairo_create(surface);

    uidbg_suppress(TRUE);
    stage_render(NULL, cr, width, height, NULL);
    uidbg_suppress(FALSE);

    cairo_destroy(cr);
    return surface;
}

static JsonObject *cmd_state(JsonObject *args)
{
    JsonObject      *o = json_object_new();
    JsonObject      *win = json_object_new();
    JsonObject      *play = json_object_new();
    JsonObject      *lyr = json_object_new();
    JsonObject      *gl = json_object_new();
    JsonObject      *frames = json_object_new();
    const APPconfig *cfg = config_get();
    g_autofree gchar *label = player_label();
    g_autofree gchar *track_note = NULL;
    const AudioTrack *track = config_track_at(cfg, (gsize)MAX(app.track, 0));
    int              w = 0, h = 0;

    (void)args;

    if (g_window != NULL) {
        w = gtk_widget_get_width(g_window);
        h = gtk_widget_get_height(g_window);
    }

    json_object_set_int_member(win, "w", w);
    json_object_set_int_member(win, "h", h);
    json_object_set_boolean_member(win, "fullscreen",
        g_window != NULL && gtk_window_is_fullscreen(GTK_WINDOW(g_window)));
    json_object_set_double_member(win, "scale",
        g_window != NULL ? (double)gtk_widget_get_scale_factor(g_window) : 1.0);

    if (track != NULL)
        track_note = g_strdup(config_track_note(cfg, track));

    obj_set_str(play, "label", label);
    json_object_set_int_member(play, "track", app.track);
    json_object_set_int_member(play, "tracks", (gint64)cfg->n_tracks);
    obj_set_opt_str(play, "id", track != NULL ? track->id : NULL);
    obj_set_opt_str(play, "note", track_note);
    obj_set_opt_str(play, "resolved_title", app.resolved_title);
    obj_set_opt_str(play, "source_error", app.source_error);
    json_object_set_boolean_member(play, "source_pending", app.source_pending);
    json_object_set_boolean_member(play, "cached_source", app.source_cached);
    json_object_set_double_member(play, "position", media_position(&app.media));
    json_object_set_double_member(play, "duration", media_duration(&app.media));
    json_object_set_double_member(play, "volume", media_volume(&app.media));
    json_object_set_boolean_member(play, "muted", media_muted(&app.media));
    obj_set_opt_str(play, "device", media_output_device_name(&app.media));
    obj_set_str(play, "repeat", config_repeat_name(app.repeat));
    json_object_set_boolean_member(play, "shuffle", app.shuffle);
    json_object_set_string_member(play, "state",
        media_error(&app.media) != NULL ? "error" :
        media_is_loading(&app.media) ? "loading" :
        media_is_playing(&app.media) ? "playing" :
        media_is_paused(&app.media) ? "paused" :
        media_is_finished(&app.media) ? "finished" : "stopped");
    obj_set_opt_str(play, "error", media_error(&app.media));

    obj_set_opt_str(lyr, "line", app.lyric_line);
    obj_set_opt_str(lyr, "next", app.lyric_next);
    obj_set_opt_str(lyr, "spec", app.lyrics_spec);
    obj_set_opt_str(lyr, "error", app.lyric_error);
    json_object_set_boolean_member(lyr, "pending", app.lyrics_pending);
    json_object_set_int_member(lyr, "offset_ms", app.lrc_offset_ms);

    json_object_set_boolean_member(gl, "backdrop", backdropgl_ready());
    json_object_set_boolean_member(gl, "lyric", lyricgl_ready());
    json_object_set_boolean_member(gl, "wall", wallgl_ready());

    json_object_set_double_member(frames, "fps", frame_fps());
    json_object_set_double_member(frames, "avg_ms", frame_avg_ms());
    json_object_set_double_member(frames, "p95_ms", frame_p95_ms());

    json_object_set_object_member(o, "window", win);
    json_object_set_object_member(o, "playback", play);
    json_object_set_object_member(o, "lyrics", lyr);
    json_object_set_object_member(o, "gl", gl);
    json_object_set_object_member(o, "frames", frames);
    json_object_set_string_member(o, "view",
        app.help ? "help" : playlist_is_open() ? "wall" : "stage");
    json_object_set_boolean_member(o, "help", app.help);
    json_object_set_boolean_member(o, "wall_open", playlist_is_open());
    json_object_set_boolean_member(o, "palette_open", palette_is_open());
    obj_set_str(o, "viz", config_viz_effect());
    json_object_set_boolean_member(o, "animations", config_animations());

    return o;
}

static JsonObject *cmd_screen(JsonObject *args)
{
    JsonObject *o = json_object_new();
    (void)args;
    JsonArray  *texts = json_array_new();
    JsonArray  *rects = json_array_new();
    JsonArray  *widgets = json_array_new();
    JsonObject *size = json_object_new();
    int         w = 0, h = 0;

    if (g_window != NULL) {
        w = gtk_widget_get_width(g_window);
        h = gtk_widget_get_height(g_window);
    }

    json_object_set_int_member(size, "w", w);
    json_object_set_int_member(size, "h", h);
    json_object_set_object_member(o, "size", size);

    for (int layer = 0; layer < UIDBG_LAYER_MAX; layer++) {
        GPtrArray *items = layer_items(layer, FALSE);

        if (items == NULL)
            continue;

        for (guint i = 0; i < items->len; i++) {
            Item       *it = g_ptr_array_index(items, i);
            JsonObject *e  = json_object_new();
            g_autofree gchar *hex = color_hex(it->color);

            obj_set_str(e, "layer", g_layer_name[layer]);
            json_object_set_double_member(e, "x", it->x);
            json_object_set_double_member(e, "y", it->y);
            json_object_set_double_member(e, "w", it->w);
            json_object_set_double_member(e, "h", it->h);
            json_object_set_double_member(e, "px", it->px);
            obj_set_str(e, "color", hex);
            json_object_set_double_member(e, "a", it->color.a);

            if (it->text != NULL) {
                obj_set_str(e, "t", it->text);
                json_array_add_object_element(texts, e);
            } else {
                obj_set_str(e, "tag", it->tag);
                json_array_add_object_element(rects, e);
            }
        }
    }

    if (g_window != NULL) {
        guint count = 0;

        collect_widgets(g_window, g_window, widgets, &count, 300);
    }

    json_object_set_array_member(o, "text", texts);
    json_object_set_array_member(o, "rects", rects);
    json_object_set_array_member(o, "widgets", widgets);

    return o;
}

static JsonObject *cmd_key(JsonObject *args)
{
    JsonObject *o = json_object_new();
    const gchar *spec = json_object_get_string_member_with_default(args, "key", "");
    GdkModifierType state = 0;
    g_auto(GStrv) parts = NULL;
    guint keyval = 0;

    parts = g_strsplit(spec, "+", -1);

    for (gsize i = 0; parts[i] != NULL; i++) {
        const gchar *part = g_strstrip(parts[i]);

        if (parts[i + 1] != NULL) {
            if (g_ascii_strcasecmp(part, "ctrl") == 0)
                state |= GDK_CONTROL_MASK;
            else if (g_ascii_strcasecmp(part, "shift") == 0)
                state |= GDK_SHIFT_MASK;
            else if (g_ascii_strcasecmp(part, "alt") == 0)
                state |= GDK_ALT_MASK;
            else if (g_ascii_strcasecmp(part, "super") == 0)
                state |= GDK_SUPER_MASK;
            continue;
        }

        keyval = gdk_keyval_from_name(part);
    }

    /* 允许 "Tab" 这种名字之外，再兜一层单字符 */
    if (keyval == 0 && g_utf8_strlen(spec, -1) == 1)
        keyval = gdk_unicode_to_keyval(g_utf8_get_char(spec));

    if (keyval == 0) {
        obj_set_str(o, "error", "unknown key name");
        json_object_set_boolean_member(o, "handled", FALSE);
        return o;
    }

    json_object_set_int_member(o, "keyval", keyval);
    json_object_set_boolean_member(o, "handled", uidbg_hook_key(keyval, state));

    return o;
}

static JsonObject *cmd_click(JsonObject *args)
{
    JsonObject *o = json_object_new();
    double x = json_object_get_double_member_with_default(args, "x", 0.0);
    double y = json_object_get_double_member_with_default(args, "y", 0.0);
    gboolean twice = json_object_get_boolean_member_with_default(args, "double", FALSE);

    uidbg_hook_click(x, y, twice);

    json_object_set_double_member(o, "x", x);
    json_object_set_double_member(o, "y", y);
    json_object_set_boolean_member(o, "ok", TRUE);

    return o;
}

static JsonObject *cmd_scroll(JsonObject *args)
{
    JsonObject *o = json_object_new();
    double x  = json_object_get_double_member_with_default(args, "x", 0.0);
    double y  = json_object_get_double_member_with_default(args, "y", 0.0);
    double dx = json_object_get_double_member_with_default(args, "dx", 0.0);
    double dy = json_object_get_double_member_with_default(args, "dy", 0.0);

    uidbg_hook_scroll(x, y, dx, dy);

    json_object_set_boolean_member(o, "ok", playlist_is_open());
    return o;
}

static JsonObject *cmd_motion(JsonObject *args)
{
    JsonObject *o = json_object_new();
    double x = json_object_get_double_member_with_default(args, "x", 0.0);
    double y = json_object_get_double_member_with_default(args, "y", 0.0);

    uidbg_hook_motion(x, y);

    json_object_set_boolean_member(o, "ok", TRUE);
    return o;
}

static JsonObject *cmd_seek(JsonObject *args)
{
    JsonObject *o = json_object_new();
    double      to = json_object_get_double_member_with_default(args, "seconds", -1.0);

    if (to < 0.0)
        to = json_object_get_double_member_with_default(args, "ms", -1.0) / 1000.0;

    if (to < 0.0) {
        obj_set_str(o, "error", "need seconds (or ms)");
        return o;
    }

    uidbg_hook_seek(to);

    /* 回读一下：媒体还没 open 时 media_seek 会被直接丢掉，把真实位置
     * 回给调用方，免得对着一个没生效的 seek 做判断 */
    json_object_set_double_member(o, "seconds", to);
    json_object_set_double_member(o, "position", media_position(&app.media));
    json_object_set_boolean_member(o, "ok", TRUE);
    return o;
}

static JsonObject *cmd_text(JsonObject *args)
{
    JsonObject *o = json_object_new();
    const gchar *value = json_object_get_string_member_with_default(args, "value", "");

    uidbg_hook_text(value);

    json_object_set_string_member(o, "value", value);
    json_object_set_boolean_member(o, "palette_open", palette_is_open());
    return o;
}

static gboolean level_matches(gint level, const gchar *want)
{
    if (want == NULL || g_ascii_strcasecmp(want, "all") == 0)
        return TRUE;

    if (g_ascii_strcasecmp(want, "error") == 0)
        return (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL)) != 0;

    if (g_ascii_strcasecmp(want, "warning") == 0)
        return (level & G_LOG_LEVEL_WARNING) != 0;

    if (g_ascii_strcasecmp(want, "info") == 0)
        return (level & (G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO)) != 0;

    return TRUE;
}

static JsonObject *cmd_logs(JsonObject *args)
{
    JsonObject *o = json_object_new();
    JsonArray  *arr = json_array_new();
    gint64      limit = json_object_get_int_member_with_default(args, "limit", 60);
    const gchar *want = json_object_get_string_member_with_default(args, "level", "all");
    const gchar *contains = json_object_get_string_member_with_default(args, "contains", "");
    gint64      now = g_get_monotonic_time();
    guint       emitted = 0;

    if (limit <= 0)
        limit = 60;

    g_mutex_lock(&g_log_lock);

    /* ring 从新到旧 */
    for (guint i = 0; i < g_log_count && (gint64)emitted < limit; i++) {
        guint   idx = (g_log_head + UIDBG_LOG_RING - 1 - i) % UIDBG_LOG_RING;
        LogRec *rec = &g_logs[idx];
        JsonObject *e;

        if (!level_matches(rec->level, want))
            continue;

        if (contains != NULL && *contains != '\0' &&
            (rec->message == NULL || strstr(rec->message, contains) == NULL))
            continue;

        e = json_object_new();
        json_object_set_double_member(e, "ago", (double)(now - rec->at_us) / 1e6);
        obj_set_str(e, "level", level_name(rec->level));
        obj_set_str(e, "domain", rec->domain);
        obj_set_str(e, "msg", rec->message);
        json_array_add_object_element(arr, e);
        emitted++;
    }

    g_mutex_unlock(&g_log_lock);

    json_object_set_array_member(o, "logs", arr);
    json_object_set_int_member(o, "emitted", (gint64)emitted);
    return o;
}

static JsonObject *cmd_perf(JsonObject *args)
{
    JsonObject  *o = json_object_new();
    const gchar *renderer = "none";
    int          w = 0, h = 0;

    (void)args;

    if (g_window != NULL) {
        GskRenderer *r = gtk_native_get_renderer(GTK_NATIVE(g_window));

        w = gtk_widget_get_width(g_window);
        h = gtk_widget_get_height(g_window);

        if (r != NULL)
            renderer = G_OBJECT_TYPE_NAME(r);
    }

    json_object_set_string_member(o, "renderer", renderer);
    json_object_set_int_member(o, "w", w);
    json_object_set_int_member(o, "h", h);
    json_object_set_double_member(o, "fps", frame_fps());
    json_object_set_double_member(o, "avg_ms", frame_avg_ms());
    json_object_set_double_member(o, "p95_ms", frame_p95_ms());
    json_object_set_int_member(o, "frames", (gint64)g_frame_gap_n);
    json_object_set_double_member(o, "render_ms", (double)g_last_render_us / 1000.0);
    json_object_set_boolean_member(o, "gl_backdrop", backdropgl_ready());
    json_object_set_boolean_member(o, "gl_lyric", lyricgl_ready());
    json_object_set_boolean_member(o, "gl_wall", wallgl_ready());

    return o;
}

static JsonObject *cmd_screenshot(JsonObject *args)
{
    JsonObject *o = json_object_new();
    int         ww = 0, wh = 0;
    double      x = 0.0, y = 0.0, rw = 0.0, rh = 0.0;
    gint64      max_width = json_object_get_int_member_with_default(args, "max_width", 720);
    g_autoptr(GByteArray) png = g_byte_array_new();
    g_autofree gchar *b64 = NULL;
    cairo_surface_t *surface;
    cairo_surface_t *cropped;
    cairo_surface_t *scaled;
    JsonArray  *region;

    if (g_window == NULL) {
        obj_set_str(o, "error", "no window");
        return o;
    }

    ww = gtk_widget_get_width(g_window);
    wh = gtk_widget_get_height(g_window);

    if (ww <= 0 || wh <= 0) {
        obj_set_str(o, "error", "window has no size yet");
        return o;
    }

    surface = render_window_surface(ww, wh);

    region = json_object_has_member(args, "region")
        ? json_object_get_array_member(args, "region") : NULL;

    if (region != NULL && json_array_get_length(region) >= 4) {
        x  = json_array_get_double_element(region, 0);
        y  = json_array_get_double_element(region, 1);
        rw = json_array_get_double_element(region, 2);
        rh = json_array_get_double_element(region, 3);
    } else {
        rw = ww;
        rh = wh;
    }

    x  = CLAMP(x, 0.0, (double)ww);
    y  = CLAMP(y, 0.0, (double)wh);
    rw = CLAMP(rw, 1.0, (double)ww - x);
    rh = CLAMP(rh, 1.0, (double)wh - y);

    cropped = cairo_surface_create_for_rectangle(surface, x, y, rw, rh);

    {
        double scale = 1.0;

        if (max_width > 0)
            scale = MIN(1.0, (double)max_width / rw);

        {
            int tw = MAX(1, (int)(rw * scale));
            int th = MAX(1, (int)(rh * scale));
            cairo_t *cr;

            scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
            cr     = cairo_create(scaled);

            cairo_scale(cr, (double)tw / rw, (double)th / rh);
            cairo_set_source_surface(cr, cropped, 0.0, 0.0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
            cairo_paint(cr);
            cairo_destroy(cr);

            cairo_surface_write_to_png_stream(scaled, png_write_cb, png);

            b64 = g_base64_encode(png->data, png->len);

            json_object_set_int_member(o, "w", tw);
            json_object_set_int_member(o, "h", th);
            json_object_set_int_member(o, "bytes", (gint64)png->len);
            json_object_set_double_member(o, "scale", scale);
            json_object_set_double_member(o, "region_x", x);
            json_object_set_double_member(o, "region_y", y);
            json_object_set_double_member(o, "region_w", rw);
            json_object_set_double_member(o, "region_h", rh);
            json_object_set_string_member(o, "png", b64);

            cairo_surface_destroy(scaled);
        }
    }

    cairo_surface_destroy(cropped);
    cairo_surface_destroy(surface);

    return o;
}

/* ---------------- 请求分发 ---------------- */

static gchar *dispatch(const gchar *line)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    GError     *error = NULL;
    JsonNode   *root;
    JsonObject *req;
    JsonObject *args = NULL;
    JsonObject *data = NULL;
    JsonObject *resp = NULL;
    gint64      id = 0;
    const gchar *cmd = "";
    g_autofree gchar *body = NULL;
    GString    *out;

    if (!json_parser_load_from_data(parser, line, -1, &error)) {
        g_autofree gchar *msg = g_strdup_printf("{\"ok\":false,\"error\":\"bad json: %s\"}\n",
                                                error != NULL ? error->message : "?");
        g_clear_error(&error);
        return g_strdup(msg);
    }

    root = json_parser_get_root(parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
        return g_strdup("{\"ok\":false,\"error\":\"request must be an object\"}\n");
    }

    req = json_node_get_object(root);

    if (json_object_has_member(req, "id")) {
        JsonNode *idn = json_object_get_member(req, "id");

        if (idn != NULL && JSON_NODE_HOLDS_VALUE(idn)) {
            GType type = json_node_get_value_type(idn);

            if (type == G_TYPE_INT64)
                id = json_node_get_int(idn);
            else if (type == G_TYPE_DOUBLE)
                id = (gint64)json_node_get_double(idn);
            else if (type == G_TYPE_STRING)
                id = g_ascii_strtoll(json_node_get_string(idn), NULL, 10);
        }
    }

    cmd = json_object_get_string_member_with_default(req, "cmd", "");

    if (json_object_has_member(req, "args") &&
        JSON_NODE_HOLDS_OBJECT(json_object_get_member(req, "args")))
        args = json_object_get_object_member(req, "args");

    if (args == NULL) {
        static JsonObject *empty = NULL;

        if (empty == NULL)
            empty = json_object_new();
        args = empty;
    }

    if (g_strcmp0(cmd, "ping") == 0) {
        data = json_object_new();
        json_object_set_boolean_member(data, "pong", TRUE);
        json_object_set_int_member(data, "port", (gint64)g_port);
    } else if (g_strcmp0(cmd, "state") == 0) {
        data = cmd_state(args);
    } else if (g_strcmp0(cmd, "screen") == 0) {
        data = cmd_screen(args);
    } else if (g_strcmp0(cmd, "key") == 0) {
        data = cmd_key(args);
    } else if (g_strcmp0(cmd, "click") == 0) {
        data = cmd_click(args);
    } else if (g_strcmp0(cmd, "scroll") == 0) {
        data = cmd_scroll(args);
    } else if (g_strcmp0(cmd, "motion") == 0) {
        data = cmd_motion(args);
    } else if (g_strcmp0(cmd, "seek") == 0) {
        data = cmd_seek(args);
    } else if (g_strcmp0(cmd, "text") == 0) {
        data = cmd_text(args);
    } else if (g_strcmp0(cmd, "logs") == 0) {
        data = cmd_logs(args);
    } else if (g_strcmp0(cmd, "perf") == 0) {
        data = cmd_perf(args);
    } else if (g_strcmp0(cmd, "screenshot") == 0) {
        data = cmd_screenshot(args);
    }

    resp = json_object_new();

    json_object_set_int_member(resp, "id", id);

    if (data != NULL) {
        json_object_set_boolean_member(resp, "ok", TRUE);
        json_object_set_object_member(resp, "data", data);
    } else {
        json_object_set_boolean_member(resp, "ok", FALSE);
        json_object_set_string_member(resp, "error", "unknown command");
    }

    {
        g_autoptr(JsonGenerator) gen = json_generator_new();
        JsonNode *node = json_node_init_object(json_node_alloc(), resp);

        json_generator_set_root(gen, node);
        body = json_generator_to_data(gen, NULL);
        json_node_free(node);
    }

    out = g_string_new(body);
    g_string_append_c(out, '\n');

    return g_string_free(out, FALSE);
}

/* ---------------- 连接处理 ---------------- */

typedef struct {
    GSocketConnection *conn;
    GByteArray        *buf;
    GSource           *source;
} Conn;

static void conn_free(Conn *c)
{
    if (c == NULL)
        return;

    if (c->source != NULL)
        g_source_destroy(c->source);

    g_clear_object(&c->conn);
    g_clear_pointer(&c->buf, g_byte_array_unref);
    g_free(c);
}

static void conn_write(Conn *c, const gchar *data, gsize len)
{
    GSocket *sock = g_socket_connection_get_socket(c->conn);
    gsize    sent = 0;

    while (sent < len) {
        GError *error = NULL;
        gssize  wrote = g_socket_send_with_blocking(sock, data + sent, len - sent,
                                                   TRUE, NULL, &error);

        if (wrote <= 0) {
            g_warning("uidbg: write failed: %s",
                      error != NULL ? error->message : "?");
            g_clear_error(&error);
            return;
        }

        sent += (gsize)wrote;
    }
}

static void conn_handle_line(Conn *c, const gchar *line)
{
    g_autofree gchar *response = NULL;

    if (line == NULL || *line == '\0')
        return;

    response = dispatch(line);

    if (response != NULL)
        conn_write(c, response, strlen(response));
}

static gboolean on_conn_io(GSocket *sock, GIOCondition cond, gpointer data)
{
    Conn  *c = data;
    gchar  chunk[4096];
    GError *error = NULL;

    if ((cond & (G_IO_HUP | G_IO_ERR)) != 0 && (cond & G_IO_IN) == 0) {
        c->source = NULL;
        conn_free(c);
        return G_SOURCE_REMOVE;
    }

    for (;;) {
        gssize got = g_socket_receive(sock, chunk, sizeof chunk, NULL, &error);

        if (got < 0) {
            if (error != NULL && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
                g_clear_error(&error);
                break;
            }

            if (error != NULL)
                g_clear_error(&error);

            c->source = NULL;
            conn_free(c);
            return G_SOURCE_REMOVE;
        }

        if (got == 0) {
            /* 对端关闭 */
            c->source = NULL;
            conn_free(c);
            return G_SOURCE_REMOVE;
        }

        g_byte_array_append(c->buf, (const guint8 *)chunk, (guint)got);
    }

    /* 逐行处理 */
    for (;;) {
        guint8 *data = c->buf->data;
        guint   len  = c->buf->len;
        guint8 *nl   = memchr(data, '\n', len);
        guint   taken;
        gchar  *line;

        if (nl == NULL) {
            if (len > 8 * 1024 * 1024)   /* 单行 8MB 上限，防止吃内存 */
                g_byte_array_set_size(c->buf, 0);
            break;
        }

        taken = (guint)(nl - data);
        line  = g_strndup((const gchar *)data, taken);
        g_byte_array_remove_range(c->buf, 0, taken + 1);

        while (taken > 0 && (line[taken - 1] == '\r'))
            line[--taken] = '\0';

        conn_handle_line(c, line);
        g_free(line);

        if (c->conn == NULL)
            return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static gboolean on_incoming(GSocketService *service, GSocketConnection *conn,
                            GObject *source_object, gpointer data)
{
    Conn *c = g_new0(Conn, 1);

    (void)service;
    (void)source_object;
    (void)data;

    c->conn = g_object_ref(conn);
    c->buf  = g_byte_array_new();

    /* 读必须非阻塞：主循环线程里不能等对端。写用 send_with_blocking 强制阻塞，
     * 大响应（截图）不至于写一半就报 WOULD_BLOCK。 */
    g_socket_set_blocking(g_socket_connection_get_socket(conn), FALSE);

    c->source = g_socket_create_source(g_socket_connection_get_socket(conn),
                                       G_IO_IN | G_IO_HUP | G_IO_ERR, NULL);
    g_source_set_callback(c->source, G_SOURCE_FUNC(on_conn_io), c, NULL);
    g_source_attach(c->source, NULL);

    return TRUE;
}

/* ---------------- 生命周期 ---------------- */

void uidbg_start(GtkWidget *window)
{
    GError *error = NULL;
    const gchar *port_env;

    if (g_on)
        return;

    g_window = window;

    port_env = g_getenv("FOLLOW_UIDBG_PORT");
    if (port_env != NULL && *port_env != '\0') {
        guint parsed = (guint)g_ascii_strtoull(port_env, NULL, 10);

        if (parsed > 0 && parsed < 65536)
            g_port = parsed;
    }

    g_service = g_socket_service_new();

    /* 只绑 127.0.0.1：调试通道能注入按键/点击、读整屏文字，
     * 不能挂到 0.0.0.0 上让局域网里的人也能连。 */
    {
        GInetAddress   *loopback = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
        GSocketAddress *address  = g_inet_socket_address_new(loopback, (guint16)g_port);
        gboolean        ok       = g_socket_listener_add_address(
            G_SOCKET_LISTENER(g_service), address,
            G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
            NULL, NULL, &error);

        g_object_unref(address);
        g_object_unref(loopback);

        if (!ok) {
            g_warning("uidbg: cannot listen on 127.0.0.1:%u (%s)", g_port,
                      error != NULL ? error->message : "?");
            g_clear_error(&error);
            g_clear_object(&g_service);
            return;
        }
    }

    g_signal_connect(g_service, "incoming", G_CALLBACK(on_incoming), NULL);
    g_socket_service_start(g_service);

    g_log_prev = g_log_set_default_handler(uidbg_log_handler, NULL);

    g_on = TRUE;

    g_message("uidbg: listening on 127.0.0.1:%u (agent debug channel)", g_port);
}

void uidbg_stop(void)
{
    if (!g_on)
        return;

    g_on = FALSE;

    if (g_service != NULL) {
        g_socket_service_stop(g_service);
        g_clear_object(&g_service);
    }

    for (int i = 0; i < UIDBG_LAYER_MAX; i++)
        g_clear_pointer(&g_items[i], g_ptr_array_unref);

    g_log_set_default_handler(g_log_prev != NULL ? g_log_prev : g_log_default_handler,
                              NULL);
    g_log_prev = NULL;
}
