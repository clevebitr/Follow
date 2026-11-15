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

#include "wallgl.h"

#include <epoxy/gl.h>
#include <math.h>
#include <string.h>

#include "covercache.h"
#include "glutil.h"
#include "perf.h"
#include "playlist.h"

/* 一块单位四边形，用 uniform 的 u_rect 摆到屏幕坐标。 */
static const char *VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "uniform vec2 u_view;\n"
    "uniform vec4 u_rect;\n"
    "uniform vec2 u_offset;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = u_rect.xy + a_pos * u_rect.zw + u_offset;\n"
    "    gl_Position = vec4(p.x / u_view.x * 2.0 - 1.0,\n"
    "                       1.0 - p.y / u_view.y * 2.0, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char *FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_color;\n"
    "uniform int u_use_tex;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    vec4 c = u_use_tex != 0 ? texture(u_tex, v_uv) : vec4(1.0);\n"
    "    out_color = c * u_color;\n"
    "}\n";

#define TILE_CACHE_MAX      128
#define OVERLAY_CACHE_MAX    24
#define GLOW_SIZE           128
#define OVERLAY_PAD           4

/* 纹理缓存预算。2K / 4K 上磁贴纹理比 1080p 大很多，不能只按条数算。 */
#define TILE_CACHE_BUDGET    (96u * 1024u * 1024u)
#define OVERLAY_CACHE_BUDGET (24u * 1024u * 1024u)

typedef struct {
    gboolean used;
    gsize    track;
    int      w, h;
    int      scale;
    gint64   version;
    guint    flags;
    GLuint   tex;
    gsize    bytes;
    gint64   used_us;
} TexItem;

static GtkWidget  *g_area;
static gboolean    g_active;
static gboolean    g_ready;
static gboolean    g_failed;
static WallGlFrame g_frame;
static int         g_frame_cap;
static float       g_opacity = -1.0f;
static guint       g_tick_id;

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data);

static GLuint g_prog;
static GLuint g_vao;
static GLuint g_vbo;
static GLuint g_glow;
static GLint  g_u_view, g_u_rect, g_u_offset, g_u_color, g_u_use_tex, g_u_tex;

static TexItem g_tiles[TILE_CACHE_MAX];
static TexItem g_overlays[OVERLAY_CACHE_MAX];
static gsize   g_tile_bytes;
static gsize   g_overlay_bytes;

/* 环境变量 FOLLOW_WALL_DEBUG=1 时的帧时间统计 */
static gint   g_dbg_uploads;
static gint   g_dbg_frames;
static double g_dbg_ms;
static gint64 g_dbg_since;

static gint64 now_us(void)
{
    return g_get_monotonic_time();
}

/* ---------------- shader / 资源 ---------------- */

static gboolean build_program(void)
{
    g_prog = gl_util_program(VS, FS, "wallgl");

    if (g_prog == 0)
        return FALSE;

    g_u_view    = glGetUniformLocation(g_prog, "u_view");
    g_u_rect    = glGetUniformLocation(g_prog, "u_rect");
    g_u_offset  = glGetUniformLocation(g_prog, "u_offset");
    g_u_color   = glGetUniformLocation(g_prog, "u_color");
    g_u_use_tex = glGetUniformLocation(g_prog, "u_use_tex");
    g_u_tex     = glGetUniformLocation(g_prog, "u_tex");

    return TRUE;
}

/* 选中磁贴的柔光：一张白色径向 alpha 图，绘制时用 u_color 染色。 */
static void build_glow(void)
{
    guchar *pixels = g_malloc0((gsize)GLOW_SIZE * GLOW_SIZE * 4);

    for (int y = 0; y < GLOW_SIZE; y++) {
        for (int x = 0; x < GLOW_SIZE; x++) {
            double dx = (x + 0.5) / GLOW_SIZE * 2.0 - 1.0;
            double dy = (y + 0.5) / GLOW_SIZE * 2.0 - 1.0;
            double d  = sqrt(dx * dx + dy * dy);
            double a  = d >= 1.0 ? 0.0 : 0.18 * pow(1.0 - d, 1.7);
            guchar v  = (guchar)CLAMP(a * 255.0, 0.0, 255.0);
            guchar *p = pixels + ((gsize)y * GLOW_SIZE + x) * 4;

            /* 预乘 alpha 的白色 */
            p[0] = p[1] = p[2] = p[3] = v;
        }
    }

    glGenTextures(1, &g_glow);
    glBindTexture(GL_TEXTURE_2D, g_glow);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLOW_SIZE, GLOW_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    g_free(pixels);
}

static gboolean ensure_resources(void)
{
    if (g_ready)
        return TRUE;
    if (g_failed)
        return FALSE;

    if (!build_program()) {
        g_failed = TRUE;
        return FALSE;
    }

    gl_util_quad(&g_vao, &g_vbo);
    build_glow();

    g_ready = TRUE;
    gl_util_log_context("wallgl");
    return TRUE;
}

/* ---------------- 纹理 ---------------- */

static GLuint upload_surface(cairo_surface_t *surface)
{
    GLuint tex = gl_util_texture_from_surface(surface);

    if (tex != 0)
        g_dbg_uploads++;

    return tex;
}

static int cache_pick(TexItem *items, int count)
{
    int slot = -1;

    for (int i = 0; i < count; i++) {
        if (!items[i].used)
            return i;

        if (slot < 0 || items[i].used_us < items[slot].used_us)
            slot = i;
    }

    return slot;
}

/* 按预算淘汰最久未使用的纹理。need 是新纹理的字节数。 */
static void cache_trim(TexItem *items, int count, gsize *bytes, gsize budget,
                       gsize need)
{
    while (*bytes > 0 && *bytes + need > budget) {
        int victim = -1;

        for (int i = 0; i < count; i++) {
            if (!items[i].used)
                continue;

            if (victim < 0 || items[i].used_us < items[victim].used_us)
                victim = i;
        }

        if (victim < 0)
            return;

        if (items[victim].tex != 0)
            glDeleteTextures(1, &items[victim].tex);

        *bytes -= items[victim].bytes;
        memset(&items[victim], 0, sizeof(items[victim]));
    }
}

static GLuint cache_texture(TexItem *items, int count, gsize *bytes, gsize budget,
                            gsize track, int w, int h, int scale, gint64 version,
                            guint flags, cairo_surface_t *surface)
{
    int    slot;
    GLuint tex;
    gsize  need = (gsize)MAX(1, w * scale) * (gsize)MAX(1, h * scale) * 4u;

    cache_trim(items, count, bytes, budget, need);

    slot = cache_pick(items, count);
    if (slot < 0)
        return 0;

    if (items[slot].used && items[slot].tex != 0) {
        glDeleteTextures(1, &items[slot].tex);
        *bytes -= items[slot].bytes;
    }

    tex = upload_surface(surface);

    memset(&items[slot], 0, sizeof(items[slot]));

    if (tex != 0) {
        items[slot].used    = TRUE;
        items[slot].track   = track;
        items[slot].w       = w;
        items[slot].h       = h;
        items[slot].scale   = scale;
        items[slot].version = version;
        items[slot].flags   = flags;
        items[slot].tex     = tex;
        items[slot].bytes   = need;
        items[slot].used_us = now_us();
        *bytes += need;
    }

    return tex;
}

static GLuint tile_texture(const WallGlTile *tile, int scale)
{
    int             w = (int)ceil(tile->w);
    int             h = (int)ceil(tile->h);
    gint64          version = covercache_version(tile->track);
    gint64          now = now_us();
    cairo_surface_t *surface;
    int             fallback = -1;

    if (w < 8 || h < 8)
        return 0;

    for (int i = 0; i < TILE_CACHE_MAX; i++) {
        if (g_tiles[i].used && g_tiles[i].track == tile->track &&
            g_tiles[i].w == w && g_tiles[i].h == h &&
            g_tiles[i].scale == scale && g_tiles[i].version == version) {
            g_tiles[i].used_us = now;
            return g_tiles[i].tex;
        }

        if (g_tiles[i].used && g_tiles[i].track == tile->track &&
            g_tiles[i].w == w && g_tiles[i].h == h &&
            g_tiles[i].scale == scale) {
            if (fallback < 0 || g_tiles[i].used_us > g_tiles[fallback].used_us)
                fallback = i;
        }
    }

    surface = playlist_tile_surface_cached(tile->track, w, h, scale);

    if (surface == NULL) {
        /* 磁贴纹理还没生成：先继续用同一张的旧版本（避免封面加载后闪白），
         * 同时把它交给 playlist 的低优先级预热队列。 */
        playlist_tile_request(tile->track, w, h, scale);

        if (fallback >= 0) {
            g_tiles[fallback].used_us = now;
            return g_tiles[fallback].tex;
        }

        return 0;
    }

    return cache_texture(g_tiles, TILE_CACHE_MAX, &g_tile_bytes,
                         TILE_CACHE_BUDGET, tile->track, w, h, scale,
                         version, 0, surface);
}

static GLuint overlay_texture(const WallGlTile *tile, int scale)
{
    int    w = (int)ceil(tile->w);
    int    h = (int)ceil(tile->h);
    gint64 version = covercache_version(tile->track);
    guint  flags = (tile->selected ? 1u : 0u) |
                   (tile->hovered ? 2u : 0u) |
                   (tile->current ? 4u : 0u);
    gint64 now = now_us();

    if (w < 8 || h < 8 || flags == 0)
        return 0;

    for (int i = 0; i < OVERLAY_CACHE_MAX; i++) {
        if (g_overlays[i].used && g_overlays[i].track == tile->track &&
            g_overlays[i].w == w && g_overlays[i].h == h &&
            g_overlays[i].scale == scale &&
            g_overlays[i].version == version && g_overlays[i].flags == flags) {
            g_overlays[i].used_us = now;
            return g_overlays[i].tex;
        }
    }

    return cache_texture(g_overlays, OVERLAY_CACHE_MAX, &g_overlay_bytes,
                         OVERLAY_CACHE_BUDGET, tile->track, w, h, scale,
                         version, flags,
                         playlist_tile_overlay(tile->track, w, h,
                                               tile->selected, tile->hovered,
                                               tile->current, scale));
}

/* ---------------- 绘制 ---------------- */

static void quad(double x, double y, double w, double h, GLuint tex,
                 double r, double g, double b, double a)
{
    double pa = CLAMP(a, 0.0, 1.0);

    if (w <= 0.0 || h <= 0.0 || pa <= 0.0)
        return;

    glUniform4f(g_u_rect, (float)x, (float)y, (float)w, (float)h);
    glUniform4f(g_u_color, (float)(r * pa), (float)(g * pa), (float)(b * pa),
                (float)pa);
    glUniform1i(g_u_use_tex, tex != 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void draw_tint(void)
{
    const WallGlFrame *f = &g_frame;
    double             diag, rr, cx, cy;

    if (!f->tint || f->view_w <= 0.0 || f->view_h <= 0.0)
        return;

    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glUniform2f(g_u_view, (float)f->view_w, (float)f->view_h);
    glUniform2f(g_u_offset, 0.0f, (float)f->offset_y);
    glUniform1i(g_u_tex, 0);

    diag = hypot(f->view_w, f->view_h);

    /* Folia: radial-gradient at 14% 12% (accent) + 86% 82% (secondary) */
    rr = diag * 0.30;
    cx = f->view_w * 0.14;
    cy = f->view_h * 0.12;
    quad(cx - rr, cy - rr, rr * 2.0, rr * 2.0, g_glow,
         f->tint_seed_r, f->tint_seed_g, f->tint_seed_b, 1.0);

    rr = diag * 0.32;
    cx = f->view_w * 0.86;
    cy = f->view_h * 0.82;
    quad(cx - rr, cy - rr, rr * 2.0, rr * 2.0, g_glow,
         f->tint_alt_r, f->tint_alt_g, f->tint_alt_b, 1.0);

    glBindVertexArray(0);
    glUseProgram(0);
}

/* 磁贴纹理还在后台生成时的占位色：用封面主色压暗，避免白块。 */
static void draw_tile_placeholder(double x, double y, double w, double h,
                                  gsize track)
{
    const Palette *pal = covercache_palette(track);
    double r = 0.055, g = 0.060, b = 0.070;

    if (pal != NULL && pal->has) {
        r = 0.020 + pal->seed_r * 0.24;
        g = 0.022 + pal->seed_g * 0.24;
        b = 0.026 + pal->seed_b * 0.24;
    }

    quad(x, y, w, h, 0, r, g, b, 1.0);
}

static void draw_wall(int scale)
{
    const WallGlFrame *f = &g_frame;
    const WallGlTile  *sel = NULL;

    if (f->n_tiles <= 0 || f->view_w <= 0.0 || f->view_h <= 0.0)
        return;

    if (f->selected >= 0 && f->selected < f->n_tiles)
        sel = &f->tiles[f->selected];

    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glUniform2f(g_u_view, (float)f->view_w, (float)f->view_h);
    glUniform2f(g_u_offset, 0.0f, (float)f->offset_y);
    glUniform1i(g_u_tex, 0);

    /* 1) 非选中磁贴 */
    for (int i = 0; i < f->n_tiles; i++) {
        const WallGlTile *t = &f->tiles[i];
        GLuint            tex;

        if (t == sel)
            continue;

        tex = tile_texture(t, scale);

        if (tex == 0)
            draw_tile_placeholder(t->x, t->y, t->w, t->h, t->track);
        else
            /* 磁贴纹理自己已经包含“占位底 + 封面”两个版本；封面到齐后
             * 用新版本替换。这里不再乘 reveal，否则封面异步加载期间
             * 整块磁贴（包括标题文字）都会是透明的。 */
            quad(t->x, t->y, t->w, t->h, tex, 1.0, 1.0, 1.0, 1.0);
    }

    /* 2) 非选中的悬停 / 当前曲目标记 */
    for (int i = 0; i < f->n_tiles; i++) {
        const WallGlTile *t = &f->tiles[i];

        if (t == sel || (!t->hovered && !t->current))
            continue;

        quad(t->x - OVERLAY_PAD, t->y - OVERLAY_PAD,
             t->w + OVERLAY_PAD * 2.0, t->h + OVERLAY_PAD * 2.0,
             overlay_texture(t, scale), 1.0, 1.0, 1.0, 1.0);
    }

    /* 3) 压暗中间区域 */
    if (sel != NULL && f->dim_bottom > f->dim_top)
        quad(0.0, f->dim_top, f->view_w, f->dim_bottom - f->dim_top,
             0, 0.0, 0.0, 0.0, 0.30);

    /* 4) 选中磁贴：柔光 → 本体 → 描边 */
    if (sel != NULL) {
        double pop = CLAMP(f->selected_pop, 0.5, 2.0);
        double cx  = sel->x + sel->w * 0.5;
        double cy  = sel->y + sel->h * 0.5;
        double sw  = sel->w * pop;
        double sh  = sel->h * pop;
        double sx  = cx - sw * 0.5;
        double sy  = cy - sh * 0.5;

        if (f->glow) {
            double rr = MAX(sel->w, sel->h) * 0.68;

            quad(cx - rr, cy - rr, rr * 2.0, rr * 2.0, g_glow,
                 f->glow_r, f->glow_g, f->glow_b, 1.0);
        }

        {
            GLuint stex = tile_texture(sel, scale);

            if (stex == 0)
                draw_tile_placeholder(sx, sy, sw, sh, sel->track);
            else
                quad(sx, sy, sw, sh, stex, 1.0, 1.0, 1.0, 1.0);
        }
        quad(sx - OVERLAY_PAD, sy - OVERLAY_PAD,
             sw + OVERLAY_PAD * 2.0, sh + OVERLAY_PAD * 2.0,
             overlay_texture(sel, scale), 1.0, 1.0, 1.0, 1.0);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

/* gtk_widget_set_opacity 每帧都调会把 GLArea 逼进离屏合成。
 * 只在进场 / 退场时改变，稳定在 1.0。 */
static void set_opacity(float target)
{
    target = (float)CLAMP(target, 0.0, 1.0);

    if (target >= 0.999f)
        target = 1.0f;
    if (target <= 0.001f)
        target = 0.0f;

    if (g_opacity >= 0.0f && fabsf(target - g_opacity) < 0.004f &&
        !(target == 1.0f && g_opacity != 1.0f))
        return;

    g_opacity = target;
    gtk_widget_set_opacity(g_area, (double)target);
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *context, gpointer data)
{
    int     w = gtk_widget_get_width(GTK_WIDGET(area));
    int     h = gtk_widget_get_height(GTK_WIDGET(area));
    int     scale;
    gint64  t0 = 0;

    (void)context;
    (void)data;

    if (!ensure_resources() || w <= 0 || h <= 0)
        return TRUE;

    perf_phase_begin(PERF_PHASE_WALL);

    scale = gl_util_viewport(GTK_WIDGET(area));

    if (g_getenv("FOLLOW_WALL_DEBUG") != NULL)
        t0 = now_us();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    /* 目标 alpha 保持不变：GLArea 整体必须是不透明的，
     * 否则中间的压暗区会让下面的图层透出来。 */
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ZERO, GL_ONE);

    /* 关掉墙时保持 framebuffer 全透明：GLArea 仍然可以留在窗口里预热，
     * 不会盖住下面的主画面。 */
    if (!g_active) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        perf_phase_end(PERF_PHASE_WALL);
        return TRUE;
    }

    glClearColor((float)g_frame.bg_r, (float)g_frame.bg_g,
                 (float)g_frame.bg_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (g_frame.tint)
        draw_tint();

    if (g_frame.vis > 0.001)
        draw_wall(scale);

    if (t0 != 0) {
        gint64 now = now_us();

        g_dbg_ms     += (double)(now - t0) / 1000.0;
        g_dbg_frames += 1;

        if (g_dbg_since == 0)
            g_dbg_since = now;

        if (now - g_dbg_since >= 1000000) {
            g_message("wallgl: %d frame/s, %.2f ms/frame, %d tile(s), %d upload(s)",
                      g_dbg_frames, g_dbg_ms / MAX(g_dbg_frames, 1),
                      g_frame.n_tiles, g_dbg_uploads);
            g_dbg_ms      = 0.0;
            g_dbg_frames  = 0;
            g_dbg_uploads = 0;
            g_dbg_since   = now;
        }
    }

    perf_phase_end(PERF_PHASE_WALL);
    return TRUE;
}

static void on_map(GtkWidget *widget, gpointer data)
{
    (void)data;

    /* 新映射的 GL framebuffer 是空的，强制下一帧重建。 */
    g_frame.view_w = -1.0;
    g_frame.view_h = -1.0;

    if (g_active && g_ready && g_tick_id == 0)
        g_tick_id = gtk_widget_add_tick_callback(widget, on_tick, NULL, NULL);
}

static void on_unmap(GtkWidget *widget, gpointer data)
{
    (void)data;

    if (g_tick_id != 0) {
        gtk_widget_remove_tick_callback(widget, g_tick_id);
        g_tick_id = 0;
    }
}

static void on_realize(GtkWidget *widget, gpointer data)
{
    GdkGLContext *context;
    GError       *error;

    (void)data;

    error = gtk_gl_area_get_error(GTK_GL_AREA(widget));

    if (error != NULL) {
        g_warning("wallgl: %s (falling back to cairo)", error->message);
        gtk_widget_set_opacity(widget, 0.0);
        g_failed = TRUE;
        return;
    }

    context = gtk_gl_area_get_context(GTK_GL_AREA(widget));

    if (context == NULL)
        return;

    gdk_gl_context_make_current(context);

    if (!ensure_resources()) {
        gtk_widget_set_opacity(widget, 0.0);
        return;
    }

    if (!g_active)
        gtk_widget_set_opacity(widget, 0.0);

    g_frame.view_w = -1.0;
    g_frame.view_h = -1.0;

    if (g_active && gtk_widget_get_mapped(widget) && g_tick_id == 0)
        g_tick_id = gtk_widget_add_tick_callback(widget, on_tick, NULL, NULL);
}

/* 由 GTK 的 frame clock 驱动：在 vsync 上重建一帧并提交绘制。
 * 16ms 泵只在 pending 时 queue_draw，让这个回调持续被调用。 */
static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    int w, h;

    (void)clock;
    (void)data;

    if (!g_ready || !playlist_is_open())
        return G_SOURCE_CONTINUE;

    w = gtk_widget_get_width(widget);
    h = gtk_widget_get_height(widget);

    if (w <= 0 || h <= 0)
        return G_SOURCE_CONTINUE;

    if (!playlist_frame_pending() &&
        w == (int)g_frame.view_w && h == (int)g_frame.view_h)
        return G_SOURCE_CONTINUE;

    playlist_build_frame((double)w, (double)h);

    return G_SOURCE_CONTINUE;
}

/* ---------------- 对外接口 ---------------- */

static void frame_reset(void)
{
    g_frame.n_tiles  = 0;
    g_frame.selected = -1;
    g_active         = FALSE;
}

static void frame_free(void)
{
    frame_reset();
    g_clear_pointer(&g_frame.tiles, g_free);
    g_frame_cap = 0;
}

static void on_unrealize(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    if (g_tick_id != 0) {
        gtk_widget_remove_tick_callback(widget, g_tick_id);
        g_tick_id = 0;
    }

    /* 上下文销毁后所有 GL 对象都失效；纹理从 playlist 的 cairo 缓存重新上传。 */
    for (int i = 0; i < TILE_CACHE_MAX; i++)
        memset(&g_tiles[i], 0, sizeof(g_tiles[i]));
    for (int i = 0; i < OVERLAY_CACHE_MAX; i++)
        memset(&g_overlays[i], 0, sizeof(g_overlays[i]));

    g_tile_bytes = 0;
    g_overlay_bytes = 0;
    g_ready   = FALSE;
    g_prog    = 0;
    g_vao     = 0;
    g_vbo     = 0;
    g_glow    = 0;
    g_opacity = -1.0f;
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    frame_free();
    g_area     = NULL;
    g_ready    = FALSE;
    g_opacity  = -1.0f;
    g_tick_id  = 0;
}

GtkWidget *wallgl_new(void)
{
    GtkWidget *area;

    g_frame.selected = -1;
    g_opacity        = -1.0f;

    /* 没有 GPU / 驱动不支持 GL 3.3 时的显式回退开关 */
    if (g_strcmp0(g_getenv("FOLLOW_WALLGL"), "0") == 0) {
        g_failed = TRUE;
        area = gtk_drawing_area_new();
        gtk_widget_set_hexpand(area, TRUE);
        gtk_widget_set_vexpand(area, TRUE);
        gtk_widget_set_can_target(area, FALSE);
        g_signal_connect(area, "destroy", G_CALLBACK(on_destroy), NULL);
        g_area = area;
        return area;
    }

    area = gtk_gl_area_new();

    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_widget_set_can_target(area, FALSE);
    gtk_widget_set_opacity(area, 0.0);

    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(area), FALSE);
    gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(area), FALSE);
    gtk_gl_area_set_required_version(GTK_GL_AREA(area), 3, 3);

#if GTK_CHECK_VERSION(4, 6, 0)
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area), GDK_GL_API_GL);
#endif

    g_signal_connect(area, "render", G_CALLBACK(on_render), NULL);
    g_signal_connect(area, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect_after(area, "realize", G_CALLBACK(on_realize), NULL);
    g_signal_connect(area, "unrealize", G_CALLBACK(on_unrealize), NULL);
    g_signal_connect(area, "map", G_CALLBACK(on_map), NULL);
    g_signal_connect(area, "unmap", G_CALLBACK(on_unmap), NULL);

    g_area = area;

    return area;
}

gboolean wallgl_ready(void)
{
    return g_ready;
}

int wallgl_tile_scale(void)
{
    return g_area != NULL ? gl_util_scale(g_area) : 1;
}

void wallgl_set_frame(const WallGlFrame *frame)
{
    WallGlTile *keep = g_frame.tiles;
    WallGlTile *copy = NULL;
    int         cap  = g_frame_cap;
    int         n    = frame != NULL ? frame->n_tiles : 0;

    if (g_area == NULL || frame == NULL)
        return;

    if (n > 0 && frame->tiles != NULL) {
        if (n > cap) {
            keep = g_renew(WallGlTile, keep, n);
            cap  = n;
        }

        memcpy(keep, frame->tiles, (gsize)n * sizeof(WallGlTile));
        copy = keep;
    }

    g_frame        = *frame;
    g_frame.tiles  = copy;
    g_frame.n_tiles = (n > 0 && copy != NULL) ? n : 0;
    g_frame_cap    = cap;
    g_active       = TRUE;

    set_opacity((float)frame->vis);
    gtk_widget_queue_draw(g_area);
}

void wallgl_set_active(gboolean active)
{
    if (g_area == NULL)
        return;

    g_active = active;

    if (active) {
        if (g_ready && gtk_widget_get_mapped(g_area) && g_tick_id == 0)
            g_tick_id = gtk_widget_add_tick_callback(g_area, on_tick, NULL, NULL);
    } else {
        if (g_tick_id != 0) {
            gtk_widget_remove_tick_callback(g_area, g_tick_id);
            g_tick_id = 0;
        }

        frame_free();
        set_opacity(0.0f);
    }

    gtk_widget_queue_draw(g_area);
}

void wallgl_clear(void)
{
    frame_free();

    for (int i = 0; i < TILE_CACHE_MAX; i++)
        memset(&g_tiles[i], 0, sizeof(g_tiles[i]));
    for (int i = 0; i < OVERLAY_CACHE_MAX; i++)
        memset(&g_overlays[i], 0, sizeof(g_overlays[i]));

    g_tile_bytes    = 0;
    g_overlay_bytes = 0;
    g_area          = NULL;
    g_opacity       = -1.0f;
    g_tick_id       = 0;
}
