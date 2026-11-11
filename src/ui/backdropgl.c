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

#include "backdropgl.h"

#include <epoxy/gl.h>
#include <math.h>
#include <string.h>

#include "backdrop.h"
#include "glutil.h"
#include "playlist.h"
#include "viz.h"

#define PARTICLE_TEX    64
#define VIZ_GL_MAX_VERTS 24000

static const char *VS_FULL =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.x * 2.0 - 1.0,\n"
    "                       1.0 - a_pos.y * 2.0, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char *FS_BG =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_base;\n"
    "uniform sampler2D u_prev;\n"
    "uniform int u_has_prev;\n"
    "uniform float u_tide;\n"
    "uniform vec2 u_size;\n"
    "uniform vec4 u_blob_geo[3];\n"
    "uniform vec4 u_blob_col[3];\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    vec3 color = texture(u_base, v_uv).rgb;\n"
    "    if (u_has_prev != 0) {\n"
    "        vec3 old = texture(u_prev, vec2(v_uv.x + u_tide, v_uv.y)).rgb;\n"
    "        float seam = 1.0 - u_tide;\n"
    "        float mask = 1.0 - smoothstep(seam - 0.26, seam, v_uv.x);\n"
    "        color = mix(color, old, mask);\n"
    "    }\n"
    "    vec2 px = v_uv * u_size;\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "        vec4 geo = u_blob_geo[i];\n"
    "        float d = length(px - geo.xy) / max(geo.z, 1.0);\n"
    "        float a = d < 0.45\n"
    "            ? mix(1.0, 0.42, d / 0.45)\n"
    "            : mix(0.42, 0.0, clamp((d - 0.45) / 0.55, 0.0, 1.0));\n"
    "        a = clamp(geo.w * a, 0.0, 1.0);\n"
    "        color = mix(color, u_blob_col[i].rgb, a);\n"
    "    }\n"
    "    out_color = vec4(color, 1.0);\n"
    "}\n";

static const char *FS_SCRIM =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    float t = clamp(v_uv.y, 0.0, 1.0);\n"
    "    float a = t < 0.55\n"
    "        ? mix(0.55, 0.70, t / 0.55)\n"
    "        : mix(0.70, 0.88, (t - 0.55) / 0.45);\n"
    "    out_color = vec4(0.0, 0.0, 0.0, a);\n"
    "}\n";

static const char *VS_PARTICLE =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "uniform vec2 u_view;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.x / u_view.x * 2.0 - 1.0,\n"
    "                       1.0 - a_pos.y / u_view.y * 2.0, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *FS_PARTICLE =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    float a = texture(u_tex, v_uv).a;\n"
    "    out_color = vec4(v_color.rgb * a * v_color.a, 0.0);\n"
    "}\n";

static gboolean   g_ready;
static gboolean   g_failed;

static GLuint g_prog_bg;
static GLuint g_prog_scrim;
static GLuint g_prog_particle;
static GLuint g_vao;
static GLuint g_vbo;
static GLuint g_part_vao;
static GLuint g_part_vbo;
static GLuint g_particle_tex;
static GLuint g_black_tex;

static GLuint g_base_tex;
static GLuint g_prev_tex;
static gint64 g_base_gen = -1;
static gint64 g_prev_gen = -1;

static GLint g_bg_u_base;
static GLint g_bg_u_prev;
static GLint g_bg_u_has_prev;
static GLint g_bg_u_tide;
static GLint g_bg_u_size;
static GLint g_bg_u_blob_geo;
static GLint g_bg_u_blob_col;
static GLint g_part_u_view;
static GLint g_part_u_tex;

static VizGlVertex g_verts[VIZ_GL_MAX_VERTS];

/* ---------------- 资源 ---------------- */

static void build_particle_texture(void)
{
    guchar *pixels = g_malloc0((gsize)PARTICLE_TEX * PARTICLE_TEX * 4);

    for (int y = 0; y < PARTICLE_TEX; y++) {
        for (int x = 0; x < PARTICLE_TEX; x++) {
            double u  = ((double)x + 0.5) / PARTICLE_TEX;
            double v  = ((double)y + 0.5) / PARTICLE_TEX;
            double d  = sqrt((u - 0.5) * (u - 0.5) + (v - 0.5) * (v - 0.5)) * 2.0;
            double a;

            if (d <= 0.18)
                a = 1.0 + (0.62 - 1.0) * (d / 0.18);
            else if (d <= 0.42)
                a = 0.62 + (0.22 - 0.62) * ((d - 0.18) / 0.24);
            else if (d <= 0.70)
                a = 0.22 + (0.05 - 0.22) * ((d - 0.42) / 0.28);
            else if (d < 1.0)
                a = 0.05 * (1.0 - (d - 0.70) / 0.30);
            else
                a = 0.0;

            {
                guchar v8 = (guchar)CLAMP(a * 255.0, 0.0, 255.0);
                guchar *p = pixels + ((gsize)y * PARTICLE_TEX + x) * 4;

                p[0] = p[1] = p[2] = p[3] = v8;
            }
        }
    }

    glGenTextures(1, &g_particle_tex);
    glBindTexture(GL_TEXTURE_2D, g_particle_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PARTICLE_TEX, PARTICLE_TEX, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    g_free(pixels);

    {
        const guchar black[4] = { 0, 0, 0, 255 };

        glGenTextures(1, &g_black_tex);
        glBindTexture(GL_TEXTURE_2D, g_black_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, black);
    }
}

static void build_particle_vao(void)
{
    glGenVertexArrays(1, &g_part_vao);
    glBindVertexArray(g_part_vao);
    glGenBuffers(1, &g_part_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_part_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_verts), NULL, GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VizGlVertex),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VizGlVertex),
                          (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(VizGlVertex),
                          (void *)(4 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static gboolean ensure_resources(void)
{
    if (g_ready)
        return TRUE;
    if (g_failed)
        return FALSE;

    g_prog_bg = gl_util_program(VS_FULL, FS_BG, "backdropgl/bg");
    if (g_prog_bg == 0) {
        g_failed = TRUE;
        return FALSE;
    }

    g_prog_scrim = gl_util_program(VS_FULL, FS_SCRIM, "backdropgl/scrim");
    if (g_prog_scrim == 0) {
        g_failed = TRUE;
        return FALSE;
    }

    g_prog_particle = gl_util_program(VS_PARTICLE, FS_PARTICLE,
                                      "backdropgl/particle");
    if (g_prog_particle == 0) {
        g_failed = TRUE;
        return FALSE;
    }

    g_bg_u_base     = glGetUniformLocation(g_prog_bg, "u_base");
    g_bg_u_prev     = glGetUniformLocation(g_prog_bg, "u_prev");
    g_bg_u_has_prev = glGetUniformLocation(g_prog_bg, "u_has_prev");
    g_bg_u_tide     = glGetUniformLocation(g_prog_bg, "u_tide");
    g_bg_u_size     = glGetUniformLocation(g_prog_bg, "u_size");
    g_bg_u_blob_geo = glGetUniformLocation(g_prog_bg, "u_blob_geo");
    g_bg_u_blob_col = glGetUniformLocation(g_prog_bg, "u_blob_col");

    g_part_u_view = glGetUniformLocation(g_prog_particle, "u_view");
    g_part_u_tex  = glGetUniformLocation(g_prog_particle, "u_tex");

    gl_util_quad(&g_vao, &g_vbo);
    build_particle_vao();
    build_particle_texture();

    g_ready = TRUE;
    gl_util_log_context("backdropgl");
    return TRUE;
}

/* ---------------- 绘制 ---------------- */

static void update_textures(const BackdropFrame *f)
{
    if (f->base_gen != g_base_gen) {
        if (g_base_tex != 0) {
            glDeleteTextures(1, &g_base_tex);
            g_base_tex = 0;
        }

        if (f->base != NULL)
            g_base_tex = gl_util_texture_from_surface(f->base);

        g_base_gen = f->base_gen;
    }

    if (f->prev_gen != g_prev_gen || !f->has_prev) {
        if (g_prev_tex != 0) {
            glDeleteTextures(1, &g_prev_tex);
            g_prev_tex = 0;
        }

        if (f->has_prev && f->prev != NULL)
            g_prev_tex = gl_util_texture_from_surface(f->prev);

        g_prev_gen = f->prev_gen;
    }
}

static void draw_background(const BackdropFrame *f)
{
    GLfloat geo[12];
    GLfloat col[12];

    for (int i = 0; i < 3; i++) {
        geo[i * 4 + 0] = f->blob[i].cx;
        geo[i * 4 + 1] = f->blob[i].cy;
        geo[i * 4 + 2] = f->blob[i].r;
        geo[i * 4 + 3] = f->blob[i].alpha;
        col[i * 4 + 0] = f->blob[i].cr;
        col[i * 4 + 1] = f->blob[i].cg;
        col[i * 4 + 2] = f->blob[i].cb;
        col[i * 4 + 3] = 1.0f;
    }

    glUseProgram(g_prog_bg);
    glBindVertexArray(g_vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_base_tex != 0 ? g_base_tex : g_black_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_prev_tex != 0 ? g_prev_tex : g_black_tex);

    glUniform1i(g_bg_u_base, 0);
    glUniform1i(g_bg_u_prev, 1);
    glUniform1i(g_bg_u_has_prev,
                (f->has_prev && g_prev_tex != 0) ? 1 : 0);
    glUniform1f(g_bg_u_tide, (float)f->tide);
    glUniform2f(g_bg_u_size, (float)f->base_w, (float)f->base_h);
    glUniform4fv(g_bg_u_blob_geo, 3, geo);
    glUniform4fv(g_bg_u_blob_col, 3, col);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glUseProgram(0);

    glActiveTexture(GL_TEXTURE0);
}

static void draw_particles(const BackdropFrame *f, double w, double h)
{
    int n = viz_gl_vertices(g_verts, VIZ_GL_MAX_VERTS, w, h, &f->palette);

    if (n <= 0)
        return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    glUseProgram(g_prog_particle);
    glBindVertexArray(g_part_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_part_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (gsize)n * sizeof(VizGlVertex), g_verts);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_particle_tex);
    glUniform1i(g_part_u_tex, 0);
    glUniform2f(g_part_u_view, (float)w, (float)h);

    glDrawArrays(GL_TRIANGLES, 0, n);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

static void draw_scrim(void)
{
    glEnable(GL_BLEND);
    /* RGB 压暗，但保留背景已经写好的 alpha = 1，
     * 否则整块 GLArea 会变成半透明。 */
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ZERO, GL_ONE);

    glUseProgram(g_prog_scrim);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *context, gpointer data)
{
    GtkWidget *widget = GTK_WIDGET(area);
    int        w = gtk_widget_get_width(widget);
    int        h = gtk_widget_get_height(widget);
    const BackdropFrame *f;

    (void)context;
    (void)data;

    if (!ensure_resources() || w <= 0 || h <= 0) {
        if (gtk_gl_area_get_error(GTK_GL_AREA(widget)) == NULL) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        return TRUE;
    }

    gl_util_viewport(widget);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glClearColor(0.035f, 0.035f, 0.040f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    f = backdrop_prepare(w, h);
    update_textures(f);

    draw_background(f);
    draw_particles(f, (double)w, (double)h);
    draw_scrim();

    return TRUE;
}

static void on_realize(GtkWidget *widget, gpointer data)
{
    GdkGLContext *context;
    GError       *error;

    (void)data;

    error = gtk_gl_area_get_error(GTK_GL_AREA(widget));

    if (error != NULL) {
        g_warning("backdropgl: %s (falling back to cairo background)",
                  error->message);
        g_failed = TRUE;
        gtk_widget_set_visible(widget, FALSE);
        return;
    }

    context = gtk_gl_area_get_context(GTK_GL_AREA(widget));

    if (context == NULL)
        return;

    gdk_gl_context_make_current(context);

    if (!ensure_resources()) {
        gtk_widget_set_visible(widget, FALSE);
        return;
    }

    gtk_widget_set_opacity(widget, 1.0);
}

static void on_unrealize(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    /* GtkGLArea 隐藏 / 重新映射时上下文会换一个，旧的 program / texture
     * 全部作废；下次 realize 重新建。 */
    g_ready        = FALSE;
    g_base_tex     = 0;
    g_prev_tex     = 0;
    g_base_gen     = -1;
    g_prev_gen     = -1;
    g_prog_bg      = 0;
    g_prog_scrim   = 0;
    g_prog_particle = 0;
    g_vao          = 0;
    g_vbo          = 0;
    g_part_vao     = 0;
    g_part_vbo     = 0;
    g_particle_tex = 0;
    g_black_tex    = 0;
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    g_ready         = FALSE;
    g_base_tex      = 0;
    g_prev_tex      = 0;
    g_base_gen      = -1;
    g_prev_gen      = -1;
}

/* ---------------- 对外接口 ---------------- */

GtkWidget *backdropgl_new(void)
{
    GtkWidget *area;

    if (g_strcmp0(g_getenv("FOLLOW_BACKDROPGL"), "0") == 0) {
        g_failed = TRUE;
        return NULL;
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

    return area;
}

gboolean backdropgl_ready(void)
{
    return g_ready;
}

void backdropgl_clear(void)
{
    g_ready         = FALSE;
    g_base_tex      = 0;
    g_prev_tex      = 0;
    g_base_gen      = -1;
    g_prev_gen      = -1;
}
