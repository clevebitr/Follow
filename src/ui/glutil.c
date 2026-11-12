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

#include "glutil.h"

#include <string.h>

#include "perf.h"

static const float QUAD[] = {
    /* x, y, u, v */
    0.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 1.0f,
    1.0f, 0.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
    0.0f, 1.0f, 0.0f, 1.0f,
};

static GLuint compile(GLenum type, const char *src, const char *tag)
{
    GLuint shader = glCreateShader(type);
    GLint  ok     = 0;

    if (shader == 0)
        return 0;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        GLchar log[1024] = { 0 };

        glGetShaderInfoLog(shader, sizeof(log) - 1, NULL, log);
        g_warning("%s: shader compile failed: %s", tag, log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint gl_util_program(const char *vs_src, const char *fs_src, const char *tag)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src, tag);
    GLuint fs;
    GLuint prog;
    GLint  ok = 0;

    if (vs == 0)
        return 0;

    fs = compile(GL_FRAGMENT_SHADER, fs_src, tag);

    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }

    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!ok) {
        GLchar log[1024] = { 0 };

        glGetProgramInfoLog(prog, sizeof(log) - 1, NULL, log);
        g_warning("%s: program link failed: %s", tag, log);
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

void gl_util_quad(GLuint *vao, GLuint *vbo)
{
    *vao = 0;
    *vbo = 0;

    glGenVertexArrays(1, vao);
    glBindVertexArray(*vao);
    glGenBuffers(1, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

GLuint gl_util_texture_from_surface(cairo_surface_t *surface)
{
    GLuint        tex = 0;
    int           w, h, stride;
    const guchar *data;

    if (surface == NULL ||
        cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS ||
        cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32)
        return 0;

    cairo_surface_flush(surface);

    w      = cairo_image_surface_get_width(surface);
    h      = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);
    data   = cairo_image_surface_get_data(surface);

    if (w <= 0 || h <= 0 || data == NULL)
        return 0;

    glGenTextures(1, &tex);
    if (tex == 0)
        return 0;

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

    perf_note_upload((gsize)w * (gsize)h * 4u);

    return tex;
}

static int widget_scale(GtkWidget *widget)
{
    int scale;

    if (widget == NULL)
        return 1;

    scale = gtk_widget_get_scale_factor(widget);

    return scale > 0 ? scale : 1;
}

int gl_util_scale(GtkWidget *widget)
{
    return widget_scale(widget);
}

int gl_util_viewport(GtkWidget *widget)
{
    int scale = widget_scale(widget);
    int w     = gtk_widget_get_width(widget);
    int h     = gtk_widget_get_height(widget);

    glViewport(0, 0, MAX(1, w * scale), MAX(1, h * scale));

    return scale;
}

const char *gl_util_renderer(void)
{
    const GLubyte *renderer = glGetString(GL_RENDERER);

    return renderer != NULL ? (const char *)renderer : "?";
}

gboolean gl_util_software(void)
{
    gchar *lower;
    static const char *needles[] = {
        "llvmpipe", "softpipe", "swiftshader", "software",
        "basic render", "gdi generic", "microsoft basic",
    };
    gboolean found = FALSE;

    lower = g_ascii_strdown(gl_util_renderer(), -1);

    for (gsize i = 0; i < G_N_ELEMENTS(needles); i++) {
        if (strstr(lower, needles[i]) != NULL) {
            found = TRUE;
            break;
        }
    }

    g_free(lower);
    return found;
}

void gl_util_log_context(const char *tag)
{
    const GLubyte *version  = glGetString(GL_VERSION);
    const GLubyte *renderer = glGetString(GL_RENDERER);

    g_message("%s: OpenGL %s / %s%s", tag,
              version != NULL ? (const char *)version : "?",
              renderer != NULL ? (const char *)renderer : "?",
              gl_util_software() ? " (software rasterizer)" : "");
}
