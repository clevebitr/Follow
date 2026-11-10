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
#include "playlist.h"

/* 一块单位四边形，用 uniform 的 u_rect 摆到屏幕坐标。 */
static const float QUAD[] = {
    0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,
    1.0f, 0.0f,  1.0f, 1.0f,  0.0f, 1.0f,
};

static const char *VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "uniform vec2 u_view;\n"
    "uniform vec4 u_rect;\n"
    "uniform vec2 u_offset;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = u_rect.xy + a_pos * u_rect.zw + u_offset;\n"
    "    gl_Position = vec4(p.x / u_view.x * 2.0 - 1.0,\n"
    "                       1.0 - p.y / u_view.y * 2.0, 0.0, 1.0);\n"
    "    v_uv = a_pos;\n"
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

#define TILE_CACHE_MAX    128
#define OVERLAY_CACHE_MAX  24
#define GLOW_SIZE         128
#define OVERLAY_PAD       4

typedef struct {
    gboolean used;
    gsize    track;
    int      w, h;
    gint64   version;
    guint    flags;
    GLuint   tex;
    gint64   used_us;
} TexItem;

static GtkWidget  *g_area;
static gboolean    g_active;
static gboolean    g_ready;
static gboolean    g_failed;
static WallGlFrame g_frame;
static int         g_frame_cap;

static GLuint g_prog;
static GLuint g_vao;
static GLuint g_vbo;
static GLuint g_glow;
static GLint  g_u_view, g_u_rect, g_u_offset, g_u_color, g_u_use_tex, g_u_tex;

static TexItem g_tiles[TILE_CACHE_MAX];
static TexItem g_overlays[OVERLAY_CACHE_MAX];

/* 环境变量 FOLLOW_WALL_DEBUG=1 时的帧时间统计 */
static gint   g_dbg_uploads;
static gint   g_dbg_frames;
static double g_dbg_ms;
static gint64 g_dbg_since;

static gint64 now_us(void)
{
    return g_get_monotonic_time();
}

/* ---------------- shader ---------------- */

static GLuint compile(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint  ok = 0;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        GLchar log[1024] = { 0 };

        glGetShaderInfoLog(shader, sizeof(log) - 1, NULL, log);
        g_warning("wallgl: shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static gboolean build_program(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, VS);
    GLuint fs;
    GLint  ok = 0;

    if (vs == 0)
        return FALSE;

    fs = compile(GL_FRAGMENT_SHADER, FS);
    if (fs == 0) {
        glDeleteShader(vs);
        return FALSE;
    }

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!ok) {
        GLchar log[1024] = { 0 };

        glGetProgramInfoLog(g_prog, sizeof(log) - 1, NULL, log);
        g_warning("wallgl: program link failed: %s", log);
        glDeleteProgram(g_prog);
        g_prog = 0;
        return FALSE;
    }

    g_u_view    = glGetUniformLocation(g_prog, "u_view");
    g_u_rect    = glGetUniformLocation(g_prog, "u_rect");
    g_u_offset  = glGetUniformLocation(g_prog, "u_offset");
    g_u_color   = glGetUniformLocation(g_prog, "u_color");
    g_u_use_tex = glGetUniformLocation(g_prog, "u_use_tex");
    g_u_tex     = glGetUniformLocation(g_prog, "u_tex");

    return TRUE;
}

static void build_quad(void)
{
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);
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

    build_quad();
    build_glow();

    g_ready = TRUE;
    g_message("wallgl: OpenGL %s / %s",
              glGetString(GL_VERSION) != NULL ? (const char *)glGetString(GL_VERSION) : "?",
              glGetString(GL_RENDERER) != NULL ? (const char *)glGetString(GL_RENDERER) : "?");
    return TRUE;
}

/* ---------------- 纹理 ---------------- */

static GLuint upload_surface(cairo_surface_t *surface)
{
    GLuint          tex;
    int             w, h, stride;
    const guchar   *data;

    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return 0;

    cairo_surface_flush(surface);

    w      = cairo_image_surface_get_width(surface);
    h      = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);
    data   = cairo_image_surface_get_data(surface);

    if (w <= 0 || h <= 0 || data == NULL)
        return 0;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* cairo ARGB32 在小端内存里是 B,G,R,A，正好对应 GL_BGRA；预乘 alpha 直接可用。 */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

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

static GLuint tile_texture(const WallGlTile *tile)
{
    int      w = (int)ceil(tile->w);
    int      h = (int)ceil(tile->h);
    gint64   version = covercache_version(tile->track);
    gint64   now = now_us();
    int      slot;
    GLuint   tex;

    if (w < 8 || h < 8)
        return 0;

    for (int i = 0; i < TILE_CACHE_MAX; i++) {
        if (g_tiles[i].used && g_tiles[i].track == tile->track &&
            g_tiles[i].w == w && g_tiles[i].h == h &&
            g_tiles[i].version == version) {
            g_tiles[i].used_us = now;
            return g_tiles[i].tex;
        }
    }

    slot = cache_pick(g_tiles, TILE_CACHE_MAX);
    if (slot < 0)
        return 0;

    if (g_tiles[slot].used && g_tiles[slot].tex != 0)
        glDeleteTextures(1, &g_tiles[slot].tex);

    tex = upload_surface(playlist_tile_surface(tile->track, w, h));

    g_tiles[slot].used    = tex != 0;
    g_tiles[slot].track   = tile->track;
    g_tiles[slot].w       = w;
    g_tiles[slot].h       = h;
    g_tiles[slot].version = version;
    g_tiles[slot].flags   = 0;
    g_tiles[slot].tex     = tex;
    g_tiles[slot].used_us = now;

    return tex;
}

static GLuint overlay_texture(const WallGlTile *tile)
{
    int      w = (int)ceil(tile->w);
    int      h = (int)ceil(tile->h);
    gint64   version = covercache_version(tile->track);
    guint    flags = (tile->selected ? 1u : 0u) |
                     (tile->hovered ? 2u : 0u) |
                     (tile->current ? 4u : 0u);
    gint64   now = now_us();
    int      slot;
    GLuint   tex;

    if (w < 8 || h < 8 || flags == 0)
        return 0;

    for (int i = 0; i < OVERLAY_CACHE_MAX; i++) {
        if (g_overlays[i].used && g_overlays[i].track == tile->track &&
            g_overlays[i].w == w && g_overlays[i].h == h &&
            g_overlays[i].version == version && g_overlays[i].flags == flags) {
            g_overlays[i].used_us = now;
            return g_overlays[i].tex;
        }
    }

    slot = cache_pick(g_overlays, OVERLAY_CACHE_MAX);
    if (slot < 0)
        return 0;

    if (g_overlays[slot].used && g_overlays[slot].tex != 0)
        glDeleteTextures(1, &g_overlays[slot].tex);

    tex = upload_surface(playlist_tile_overlay(tile->track, w, h,
                                               tile->selected, tile->hovered,
                                               tile->current));

    g_overlays[slot].used    = tex != 0;
    g_overlays[slot].track   = tile->track;
    g_overlays[slot].w       = w;
    g_overlays[slot].h       = h;
    g_overlays[slot].version = version;
    g_overlays[slot].flags   = flags;
    g_overlays[slot].tex     = tex;
    g_overlays[slot].used_us = now;

    return tex;
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

static void draw_wall(void)
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

        if (t == sel)
            continue;

        quad(t->x, t->y, t->w, t->h, tile_texture(t), 1.0, 1.0, 1.0, t->reveal);
    }

    /* 2) 非选中的悬停 / 当前曲目标记 */
    for (int i = 0; i < f->n_tiles; i++) {
        const WallGlTile *t = &f->tiles[i];

        if (t == sel || (!t->hovered && !t->current))
            continue;

        quad(t->x - OVERLAY_PAD, t->y - OVERLAY_PAD,
             t->w + OVERLAY_PAD * 2.0, t->h + OVERLAY_PAD * 2.0,
             overlay_texture(t), 1.0, 1.0, 1.0, 1.0);
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

        quad(sx, sy, sw, sh, tile_texture(sel), 1.0, 1.0, 1.0, sel->reveal);
        quad(sx - OVERLAY_PAD, sy - OVERLAY_PAD,
             sw + OVERLAY_PAD * 2.0, sh + OVERLAY_PAD * 2.0,
             overlay_texture(sel), 1.0, 1.0, 1.0, 1.0);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *context, gpointer data)
{
    int     w = gtk_widget_get_width(GTK_WIDGET(area));
    int     h = gtk_widget_get_height(GTK_WIDGET(area));
    gint64  t0 = 0;

    (void)context;
    (void)data;

    if (!ensure_resources() || w <= 0 || h <= 0)
        return TRUE;

    if (g_getenv("FOLLOW_WALL_DEBUG") != NULL)
        t0 = now_us();

    /* 启动时先用 1% 透明度让 GL 层完成初始化和 shader 编译，
     * 就绪后彻底隐藏，避免第一帧才初始化造成卡顿。 */
    if (!g_active && gtk_widget_get_opacity(GTK_WIDGET(area)) > 0.0)
        gtk_widget_set_opacity(GTK_WIDGET(area), 0.0);

    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor((float)g_frame.bg_r, (float)g_frame.bg_g, (float)g_frame.bg_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (g_active && g_frame.tint)
        draw_tint();

    if (g_active && g_frame.vis > 0.001)
        draw_wall();

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

    return TRUE;
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

static void on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    frame_free();
    g_area   = NULL;
    g_ready  = FALSE;
}

GtkWidget *wallgl_new(void)
{
    GtkWidget *area;

    g_frame.selected = -1;

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
    gtk_widget_set_opacity(area, 0.01);

    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(area), FALSE);
    gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(area), FALSE);
    gtk_gl_area_set_required_version(GTK_GL_AREA(area), 3, 3);

#if GTK_CHECK_VERSION(4, 6, 0)
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area), GDK_GL_API_GL);
#endif

    g_signal_connect(area, "render", G_CALLBACK(on_render), NULL);
    g_signal_connect(area, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect_after(area, "realize", G_CALLBACK(on_realize), NULL);

    g_area = area;

    return area;
}

gboolean wallgl_ready(void)
{
    return g_ready;
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

    g_frame      = *frame;
    g_frame.tiles = copy;
    g_frame.n_tiles = (n > 0 && copy != NULL) ? n : 0;
    g_frame_cap  = cap;
    g_active     = TRUE;

    gtk_widget_set_opacity(g_area, CLAMP(frame->vis, 0.0, 1.0));
    gtk_widget_queue_draw(g_area);
}

void wallgl_set_active(gboolean active)
{
    if (g_area == NULL)
        return;

    g_active = active;

    if (!active) {
        frame_free();
        gtk_widget_set_opacity(g_area, 0.0);
    }

    gtk_widget_queue_draw(g_area);
}

void wallgl_clear(void)
{
    frame_free();

    for (int i = 0; i < TILE_CACHE_MAX; i++)
        g_tiles[i].used = FALSE;
    for (int i = 0; i < OVERLAY_CACHE_MAX; i++)
        g_overlays[i].used = FALSE;

    g_area = NULL;
}
