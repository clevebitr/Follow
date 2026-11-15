/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#include "lyricgl.h"

#include <epoxy/gl.h>
#include <math.h>
#include <string.h>

#include "glutil.h"
#include "layout.h"
#include "perf.h"
#include "theme.h"
#include "uidbg.h"

#define POS_SPRING_MASS      1.0
#define POS_SLOW_STIFF      90.0
#define POS_SLOW_DAMP       15.0
#define POS_MIN_INTERVAL   100.0
#define POS_MAX_INTERVAL   800.0
#define POS_MIN_STIFF      170.0
#define POS_MAX_STIFF      220.0
#define POS_DAMP_MULT        2.2
#define POS_INTERVAL_EXP     0.2

#define GAP_MAX_K        1.6
#define STAGGER_SEC         0.05
#define STAGGER_DECAY       (1.0 / 1.05)

#define SCALE_SPRING_MASS      2.0
#define SCALE_SPRING_DAMPING  25.0
#define SCALE_SPRING_STIFF   100.0

#define SCALE_ACTIVE      1.15
#define SCALE_INACTIVE    0.94
#define SCALE_INACTIVE2   0.90
#define SCALE_PREV        0.95

#define WEIGHT_ACTIVE     PANGO_WEIGHT_HEAVY
#define WEIGHT_IDLE       PANGO_WEIGHT_BOLD

/* 模糊等级（逻辑像素）；σ = 等级 × BLUR_SIGMA_K。 */
#define BLUR_PREV         1.6
#define BLUR_P1           2.0
#define BLUR_P2           3.2
#define BLUR_EXIT         2.6
#define BLUR_MAX          4.0
/* 模糊等级到 σ 的换算系数。 */
#define BLUR_SIGMA_K      0.8
#define BLUR_RAMP_TAU     0.11
#define FADE_TAU          0.09
#define PREVIEW_FADE_TAU  0.22
#define EMERGE_EM         0.9

/* 着色收敛：着色快、褪色慢。 */
#define TINT_TAU          0.16
#define TINT_OUT_TAU      0.34

#define DIM_UNSUN         0.45
#define SWEEP_EM          0.5

#define EDGE_FADE_EM      0.7

/* 1 = 左对齐，2 = 右对齐，0 = 居中。 */
#define ALIGN_MODE        1

#define SNAP_JUMP         3

#define LYRIC_LINE_MAX          8
#define STACK_MAX         2
#define ROW_MAX           LYRIC_SLOT_N

#define ROW_PAD           16
#define SLOT_MAX          32

#define PREV_ALPHA        0.58
#define DIM_ALPHA_1       0.72

/* 非演出行使用淡灰色。 */
#define COL_LYRIC_DIM     RGBA(0.76, 0.76, 0.80, 1.0)

static const char *VS_RECT =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "uniform vec4  u_rect;\n"
    "uniform vec2  u_area;\n"
    "uniform vec2  u_origin;\n"
    "uniform float u_scale;\n"
    "out vec2 v_uv;\n"
    "out vec2 v_px;\n"
    "void main() {\n"
    "    vec2 p = u_rect.xy + a_pos * u_rect.zw;\n"
    "    p = u_origin + (p - u_origin) * u_scale;\n"
    "    gl_Position = vec4(p.x / u_area.x * 2.0 - 1.0,\n"
    "                       1.0 - p.y / u_area.y * 2.0, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_px = p;\n"
    "}\n";

static const char *FS_TEXT =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec2 v_px;\n"
    "uniform sampler2D u_tex;\n"
    "uniform sampler2D u_order;\n"
    "uniform vec3  u_color;\n"
    "uniform float u_alpha;\n"
    "uniform float u_progress;\n"
    "uniform float u_sweep;\n"
    "uniform float u_dim;\n"
    "uniform float u_lod;\n"
    "uniform float u_blur;\n"
    "uniform vec2  u_texel;\n"
    "uniform vec2  u_bounds;\n"
    "uniform vec2  u_edge;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    float a;\n"
    "    if (u_blur < 0.3) {\n"
    "        a = textureLod(u_tex, v_uv, 0.0).r;\n"
    "    } else {\n"
    "        /* 9 抽头高斯（中心 + 4 轴 + 4 对角），σ 用 u_blur 个纹素表示；\n"
    "         * mipmap 只负责把每个抽头的取样范围铺平，避免稀疏取样打出斑点。 */\n"
    "        vec2  dx = vec2(u_texel.x * u_blur, 0.0);\n"
    "        vec2  dy = vec2(0.0, u_texel.y * u_blur);\n"
    "        float c  = textureLod(u_tex, v_uv, u_lod).r;\n"
    "        float ax = textureLod(u_tex, v_uv + dx, u_lod).r + textureLod(u_tex, v_uv - dx, u_lod).r;\n"
    "        float ay = textureLod(u_tex, v_uv + dy, u_lod).r + textureLod(u_tex, v_uv - dy, u_lod).r;\n"
    "        float dg = textureLod(u_tex, v_uv + dx + dy, u_lod).r\n"
    "                 + textureLod(u_tex, v_uv + dx - dy, u_lod).r\n"
    "                 + textureLod(u_tex, v_uv - dx + dy, u_lod).r\n"
    "                 + textureLod(u_tex, v_uv - dx - dy, u_lod).r;\n"
    "        a = (c + (ax + ay) * 0.6065 + dg * 0.3679) * 0.1876;\n"
    "    }\n"
    "    if (a <= 0.0) discard;\n"
    "    float t = textureLod(u_order, v_uv, 0.0).r;\n"
    "    float p = u_progress * (1.0 + u_sweep) - u_sweep;\n"
    "    float lit = 1.0 - smoothstep(0.0, 1.0, clamp((t - p) / u_sweep, 0.0, 1.0));\n"
    "    float level = mix(u_dim, 1.0, lit);\n"
    "    float fade = 1.0;\n"
    "    if (u_edge.y > 0.5)\n"
    "        fade *= clamp((u_bounds.y - v_px.y) / u_edge.y, 0.0, 1.0);\n"
    "    if (u_edge.x > 0.5)\n"
    "        fade *= clamp((v_px.y - u_bounds.x) / u_edge.x, 0.0, 1.0);\n"
    "    out_color = vec4(u_color * a * level, a * level) * (u_alpha * fade);\n"
    "}\n";

/* 可分离高斯：13 抽头，步长 σ/2。 */
static const char *FS_BLUR =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec2 v_px;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2  u_texel;\n"
    "uniform vec2  u_uvoff;\n"
    "uniform vec2  u_dir;\n"
    "uniform float u_sigma;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    /* 离屏图只有当前行那块有内容，UV 按像素坐标换算（不能拿四边形的\n"
    "     * 0..1 当整张纹理的 UV）；纹理 v=0 在底部、像素坐标 y 向下，\n"
    "     * 所以 V 用顶对齐偏移减去像素 y */\n"
    "    vec2  uv    = vec2(v_px.x * u_texel.x, u_uvoff.y - v_px.y * u_texel.y);\n"
    "    float sigma = max(u_sigma, 0.35);\n"
    "    float step  = max(1.0, sigma * 0.5);\n"   /* 抽头间距（纹素） */
    "    vec4  acc   = texture(u_tex, uv);\n"
    "    float wsum  = 1.0;\n"
    "    for (int i = 1; i <= 6; i++) {\n"
    "        float d = float(i) * step;\n"
    "        float w = exp(-0.5 * d * d / (sigma * sigma));\n"
    "        vec2  o = u_dir * u_texel * d;\n"
    "        acc  += (texture(u_tex, uv + o) + texture(u_tex, uv - o)) * w;\n"
    "        wsum += 2.0 * w;\n"
    "    }\n"
    "    out_color = acc / wsum;\n"
    "}\n";

/* 将模糊结果合成回主 framebuffer。 */
static const char *FS_COMP =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec2 v_px;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2  u_uvscale;\n"
    "uniform vec2  u_uvoff;\n"
    "uniform float u_alpha;\n"
    "uniform vec2  u_bounds;\n"
    "uniform vec2  u_edge;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    vec2  uv   = vec2(v_px.x * u_uvscale.x, u_uvoff.y - v_px.y * u_uvscale.y);\n"
    "    vec4  c    = texture(u_tex, uv);\n"
    "    float fade = 1.0;\n"
    "    if (u_edge.y > 0.5)\n"
    "        fade *= clamp((u_bounds.y - v_px.y) / u_edge.y, 0.0, 1.0);\n"
    "    if (u_edge.x > 0.5)\n"
    "        fade *= clamp((v_px.y - u_bounds.x) / u_edge.x, 0.0, 1.0);\n"
    "    if (c.a <= 0.0 || fade <= 0.0) discard;\n"
    "    out_color = c * (u_alpha * fade);\n"
    "}\n";

/* 视觉行（折行后的一段） */
typedef struct {
    gchar  *text;
    int     n_chars;
    double  dy;
    double  h;
    double  w;
    guint8 *order;     /* 逐字时间坐标（0..255）；NULL = 整行高亮。 */
} Row;

typedef struct {
    gssize  idx;
    gchar  *text;
    Row    *rows;
    int     n_rows;
    double  h;
    double  lay_w, lay_px;
    double  lay_weight;
    gint64 *char_ms;
    gsize   n_chars;

    double  w_max;

    double  y_t, alpha_t, scale_t, blur_t, tint_t;
    /* 字色从白到淡灰的过渡。 */
    double  dim_t;
    double  y_pending, y_wait, y_delay;
    double  y, vy, alpha, scale, vscale, blur, tint, dim;
    Rgba    col;
    double  progress;

    double  alpha_tau;
    double  tint_tau;
    double  emerge;
    int     role;         /* 0 = 演出；>0 = 预演；<0 = 已唱。 */

    gboolean placed;
    gboolean alive;
    gboolean is_current;
    gboolean drawn;
} Line;

static Line g_lines[LYRIC_LINE_MAX];

static const Lyrics *g_lyrics;
static gssize  g_index = -1;
static gssize  g_focus_idx = -1;
static gboolean g_anchor_no_focus;
static LyricViewGeom g_geom;
static int     g_scale = 1;

static cairo_surface_t *g_measure_surface;
static cairo_t         *g_measure_cr;

static GtkWidget *g_area;
static gboolean   g_ready;
static gboolean   g_failed;

static GLuint g_prog_text;
static GLuint g_prog_blur;
static GLuint g_prog_comp;
static GLuint g_vao;
static GLuint g_vbo;

static GLint u_rect, u_area, u_origin, u_scale_u, u_tex, u_order, u_color, u_alpha;
static GLint u_progress, u_sweep, u_dim, u_lod, u_blur, u_texel, u_bounds, u_edge;

static GLint    b_tex, b_texel, b_uvoff, b_dir, b_sigma;
static GLint    c_tex, c_uvscale, c_uvoff, c_alpha, c_bounds, c_edge;
static GLuint   g_blur_fbo[2], g_blur_tex[2];
static int      g_blur_w, g_blur_h;
static gboolean g_blur_ok;

static int g_view_w = 1;
static int g_view_h = 1;

typedef struct {
    GLint rect, area, origin, scale;
} QuadUniforms;

static QuadUniforms g_qu_text, g_qu_blur, g_qu_comp;

static GLint g_home_fbo;

/* 播放头：位置与时间戳，tick 内线性外推。 */
static double   g_pos_ms;
static gint64   g_pos_us;
static gboolean g_pos_playing;

static guint  g_tick_id;
static gint64 g_last_us;
static gboolean g_dragging;   /* 拖动 / 缩放中暂停重绘。 */

static gint64 now_us(void)
{
    return g_get_monotonic_time();
}

static double playhead_ms(void)
{
    double pos = g_pos_ms;

    if (g_pos_playing && g_pos_us != 0)
        pos += (double)(now_us() - g_pos_us) / 1000.0;

    return pos;
}

/* 行起止时间；最后一行按 4 秒。 */
static void line_span(gssize idx, double *start_ms, double *end_ms)
{
    double start = 0.0, end = 0.0;

    if (g_lyrics != NULL && idx >= 0 && idx < (gssize)g_lyrics->n_lines) {
        start = (double)g_lyrics->lines[idx].time_ms;

        if (idx + 1 < (gssize)g_lyrics->n_lines)
            end = (double)g_lyrics->lines[idx + 1].time_ms;
    }

    if (end <= start)
        end = start + 4000.0;

    *start_ms = start;
    *end_ms   = end;
}

static double scan_progress(gssize idx)
{
    double start, end, pos;

    line_span(idx, &start, &end);
    pos = playhead_ms();

    if (pos <= start)
        return 0.0;
    if (pos >= end)
        return 1.0;

    return (pos - start) / (end - start);
}

static gboolean line_has_words(gssize idx)
{
    return g_lyrics != NULL && idx >= 0 && idx < (gssize)g_lyrics->n_lines &&
           g_lyrics->lines[idx].char_ms != NULL;
}

static gboolean ensure_resources(void)
{
    if (g_ready)
        return TRUE;
    if (g_failed)
        return FALSE;

    g_prog_text = gl_util_program(VS_RECT, FS_TEXT, "lyricgl/text");

    if (g_prog_text == 0) {
        g_failed = TRUE;
        return FALSE;
    }

    u_rect     = glGetUniformLocation(g_prog_text, "u_rect");
    u_area     = glGetUniformLocation(g_prog_text, "u_area");
    u_origin   = glGetUniformLocation(g_prog_text, "u_origin");
    u_scale_u  = glGetUniformLocation(g_prog_text, "u_scale");    u_tex      = glGetUniformLocation(g_prog_text, "u_tex");
    u_order    = glGetUniformLocation(g_prog_text, "u_order");
    u_color    = glGetUniformLocation(g_prog_text, "u_color");
    u_alpha    = glGetUniformLocation(g_prog_text, "u_alpha");
    u_progress = glGetUniformLocation(g_prog_text, "u_progress");
    u_sweep    = glGetUniformLocation(g_prog_text, "u_sweep");
    u_dim      = glGetUniformLocation(g_prog_text, "u_dim");
    u_lod      = glGetUniformLocation(g_prog_text, "u_lod");
    u_blur     = glGetUniformLocation(g_prog_text, "u_blur");
    u_texel    = glGetUniformLocation(g_prog_text, "u_texel");
    u_bounds   = glGetUniformLocation(g_prog_text, "u_bounds");
    u_edge     = glGetUniformLocation(g_prog_text, "u_edge");

    gl_util_quad(&g_vao, &g_vbo);

    /* 模糊管线：两趟高斯；失败时回退单趟。 */
    g_prog_blur = gl_util_program(VS_RECT, FS_BLUR, "lyricgl/blur");
    g_prog_comp = gl_util_program(VS_RECT, FS_COMP, "lyricgl/comp");

    if (g_prog_blur != 0 && g_prog_comp != 0) {
        g_qu_text.rect   = u_rect;
        g_qu_text.area   = u_area;
        g_qu_text.origin = u_origin;
        g_qu_text.scale  = u_scale_u;

        g_qu_blur.rect   = glGetUniformLocation(g_prog_blur, "u_rect");
        g_qu_blur.area   = glGetUniformLocation(g_prog_blur, "u_area");
        g_qu_blur.origin = glGetUniformLocation(g_prog_blur, "u_origin");
        g_qu_blur.scale  = glGetUniformLocation(g_prog_blur, "u_scale");

        g_qu_comp.rect   = glGetUniformLocation(g_prog_comp, "u_rect");
        g_qu_comp.area   = glGetUniformLocation(g_prog_comp, "u_area");
        g_qu_comp.origin = glGetUniformLocation(g_prog_comp, "u_origin");
        g_qu_comp.scale  = glGetUniformLocation(g_prog_comp, "u_scale");

        b_tex   = glGetUniformLocation(g_prog_blur, "u_tex");
        b_texel = glGetUniformLocation(g_prog_blur, "u_texel");
        b_uvoff = glGetUniformLocation(g_prog_blur, "u_uvoff");
        b_dir   = glGetUniformLocation(g_prog_blur, "u_dir");
        b_sigma = glGetUniformLocation(g_prog_blur, "u_sigma");

        c_tex    = glGetUniformLocation(g_prog_comp, "u_tex");
        c_uvscale = glGetUniformLocation(g_prog_comp, "u_uvscale");
        c_uvoff   = glGetUniformLocation(g_prog_comp, "u_uvoff");
        c_alpha  = glGetUniformLocation(g_prog_comp, "u_alpha");
        c_bounds = glGetUniformLocation(g_prog_comp, "u_bounds");
        c_edge   = glGetUniformLocation(g_prog_comp, "u_edge");

        g_blur_ok = TRUE;
    }

    g_ready = TRUE;
    gl_util_log_context("lyricgl");
    return TRUE;
}

typedef struct {
    gchar  *key;
    int     w, h;
    int     pad;
    GLuint  tex;        /* 字形遮罩（mipmap 用于模糊）。 */
    GLuint  order;      /* 逐字时间坐标（R8）。 */
    gint64  used;
} Slot;

static Slot   g_slots[SLOT_MAX];
static gint64 g_slot_clock;

static void slot_free(Slot *s)
{
    if (s->tex != 0)
        glDeleteTextures(1, &s->tex);
    if (s->order != 0)
        glDeleteTextures(1, &s->order);

    g_free(s->key);
    memset(s, 0, sizeof(*s));
}

static void upload_r8(GLuint *tex, cairo_surface_t *surface, int w, int h,
                      gboolean mipmap)
{
    const guchar *data;
    int           stride;

    cairo_surface_flush(surface);
    data   = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);

    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    perf_note_upload((gsize)w * (gsize)h);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (mipmap) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static guint32 hash_order(const guint8 *order, int n)
{
    guint32 h = 2166136261u;

    if (order == NULL)
        return 0;

    for (int i = 0; i < n; i++) {
        h ^= order[i];
        h *= 16777619u;
    }

    return h;
}

static gboolean slot_build(Slot *s, const gchar *text, double px_dev,
                           double weight, const guint8 *order, int n_order)
{
    TextStyle        style;
    cairo_surface_t *glyph = NULL;
    cairo_surface_t *ord   = NULL;
    int              w = 0, h = 0;

    style = TEXT_STYLE(FONT_MONO, px_dev, (PangoWeight)weight,
                       px_dev * 0.10, WHITE(1.0));

    if (!text_mask_pair(text, &style, ROW_PAD, order, n_order,
                        &glyph, &ord, &w, &h))
        return FALSE;

    s->w   = w;
    s->h   = h;
    s->pad = ROW_PAD;

    upload_r8(&s->tex, glyph, w, h, TRUE);
    cairo_surface_destroy(glyph);

    if (ord != NULL) {
        upload_r8(&s->order, ord, w, h, FALSE);
        cairo_surface_destroy(ord);
    }

    return s->tex != 0;
}

static Slot *slot_get(const gchar *text, double px_dev, double weight,
                      const guint8 *order, int n_order)
{
    g_autofree gchar *key = g_strdup_printf("%s|%.2f|%.0f|%08x",
                                            text, px_dev, weight,
                                            hash_order(order, n_order));
    Slot *free_slot = NULL;
    Slot *lru       = &g_slots[0];

    for (int i = 0; i < SLOT_MAX; i++) {
        if (g_slots[i].key != NULL && strcmp(g_slots[i].key, key) == 0) {
            g_slots[i].used = ++g_slot_clock;
            return &g_slots[i];
        }

        if (free_slot == NULL && g_slots[i].key == NULL)
            free_slot = &g_slots[i];

        if (g_slots[i].used < lru->used)
            lru = &g_slots[i];
    }

    if (free_slot == NULL)
        free_slot = lru;

    slot_free(free_slot);

    if (!slot_build(free_slot, text, px_dev, weight, order, n_order)) {
        slot_free(free_slot);
        return NULL;
    }

    free_slot->key  = g_steal_pointer(&key);
    free_slot->used = ++g_slot_clock;

    return free_slot;
}

/* 计算文字左边界与缩放基准点。 */
static double align_left(double col_x, double col_w, double text_w, double *origin)
{
#if ALIGN_MODE == 1
    (void)col_w;
    (void)text_w;

    *origin = col_x;
    return col_x;
#elif ALIGN_MODE == 2
    *origin = col_x + col_w;
    return col_x + col_w - text_w;
#else
    *origin = col_x + col_w * 0.5;
    return col_x + (col_w - text_w) * 0.5;
#endif
}

static Rgba mix_color(Rgba a, Rgba b, double t)
{
    t = CLAMP(t, 0.0, 1.0);

    return RGBA(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

/* 扫描头宽度（order 单位）。 */
static double sweep_for(const Row *row, const Slot *slot, double px_dev)
{
    double text_w = MAX((double)slot->w - (double)(slot->pad * 2), 1.0);
    double fade   = SWEEP_EM * px_dev;

    (void)row;

    return CLAMP(fade / text_w, 0.005, 0.5);
}

/* 模糊半径 → mipmap LOD。 */
static double blur_lod(double blur_dev)
{
    if (blur_dev <= 0.05)
        return 0.0;

    return CLAMP(log2(1.0 + 3.0 * blur_dev), 0.0, 3.5);
}

/* 将本帧歌词几何上报给 uidbg。 */
static void report_lines(void)
{
    if (!uidbg_enabled())
        return;

    uidbg_begin_layer(UIDBG_LAYER_LYRIC);
    uidbg_layer_clear(UIDBG_LAYER_LYRIC);

    uidbg_record_rect(g_geom.x, g_geom.top, g_geom.w, g_geom.height,
                      col_card, "lyric-area");

    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        const Line *l = &g_lines[i];

        if (l->text == NULL || !l->drawn)
            continue;

        for (int r = 0; r < l->n_rows; r++) {
            const Row *row = &l->rows[r];
            double     y   = l->y + row->dy;
            double     unused_x = 0.0;

            if (row->text == NULL)
                continue;

            if (y + row->h < g_geom.top ||
                y > g_geom.top + g_geom.height)
                continue;

            {
                Rgba c = l->col;

                c.a = CLAMP(l->alpha, 0.0, 1.0);
                uidbg_record_text(align_left(g_geom.x, g_geom.w,
                                             MIN(row->w, g_geom.w), &unused_x),
                                  y, MIN(row->w, g_geom.w), row->h,
                                  l->lay_px > 0.0 ? l->lay_px : g_geom.px,
                                  c, row->text);
            }
        }
    }

    uidbg_begin_layer(UIDBG_LAYER_STAGE);
}

/* 行的四边形与缩放基准。 */
typedef struct {
    double x, y, w, h;
    double ox, oy;
    double sc;
} Quad;

static void quad_bounds(const Quad *q, double *x0, double *y0,
                        double *x1, double *y1)
{
    *x0 = q->ox + (q->x - q->ox) * q->sc;
    *y0 = q->oy + (q->y - q->oy) * q->sc;
    *x1 = q->ox + (q->x + q->w - q->ox) * q->sc;
    *y1 = q->oy + (q->y + q->h - q->oy) * q->sc;
}

static void quad_set(const QuadUniforms *qu, const Quad *q)
{
    glUniform4f(qu->rect, (float)q->x, (float)q->y, (float)q->w, (float)q->h);
    glUniform2f(qu->area, (float)g_view_w, (float)g_view_h);
    glUniform2f(qu->origin, (float)q->ox, (float)q->oy);
    glUniform1f(qu->scale, (float)q->sc);
}

/* 屏幕矩形 → glScissor 坐标。 */
static void scissor_rect(double x0, double y0, double x1, double y1)
{
    int x    = (int)floor(x0);
    int top  = (int)floor(y0);
    int bot  = (int)ceil(y1);
    int w    = (int)ceil(x1) - x;
    int h    = bot - top;

    glScissor(x, g_view_h - bot, MAX(1, w), MAX(1, h));
}

static void scissor_lyric(void)
{
    double top = g_geom.top * (double)g_scale;
    double bot = (g_geom.top + g_geom.height) * (double)g_scale;

    glScissor(0, (GLint)MAX(0.0, (double)g_view_h - bot),
              g_view_w, (GLsizei)MAX(1.0, bot - top));
}

static void blur_free(void)
{
    if (g_blur_tex[0] != 0 || g_blur_tex[1] != 0)
        glDeleteTextures(2, g_blur_tex);

    if (g_blur_fbo[0] != 0 || g_blur_fbo[1] != 0)
        glDeleteFramebuffers(2, g_blur_fbo);

    memset(g_blur_tex, 0, sizeof(g_blur_tex));
    memset(g_blur_fbo, 0, sizeof(g_blur_fbo));
    g_blur_w = 0;
    g_blur_h = 0;
}

static gboolean blur_ensure(int w, int h)
{
    int aw, ah;

    if (!g_blur_ok || w <= 0 || h <= 0)
        return FALSE;

    /* 尺寸向上取整到 128，避免拖动时逐帧重建纹理。 */
    aw = (w + 127) & ~127;
    ah = (h + 127) & ~127;

    if (g_blur_w == aw && g_blur_h == ah && g_blur_fbo[0] != 0)
        return TRUE;

    blur_free();

    w = aw;
    h = ah;

    glGenTextures(2, g_blur_tex);
    glGenFramebuffers(2, g_blur_fbo);

    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, g_blur_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, g_blur_tex[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            g_warning("lyricgl: 离屏缓冲不可用，模糊退回单趟抽头");
            blur_free();
            g_blur_ok = FALSE;
            break;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_home_fbo);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!g_blur_ok)
        return FALSE;

    g_blur_w = w;
    g_blur_h = h;

    return TRUE;
}

static void bind_slot(Slot *slot)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, slot->tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, slot->order != 0 ? slot->order : slot->tex);
    glActiveTexture(GL_TEXTURE0);

    glUniform1i(u_tex, 0);
    glUniform1i(u_order, 1);
}

static void row_uniforms(const Row *row, const Slot *slot, const Quad *q,
                         double px_dev)
{
    quad_set(&g_qu_text, q);
    glUniform1f(u_sweep, (float)sweep_for(row, slot, px_dev));
    glUniform2f(u_texel, 1.0f / (float)MAX(slot->w, 1),
                1.0f / (float)MAX(slot->h, 1));
    /* 区域渐隐参照边；漏设会使整行乘为 0。 */
    glUniform2f(u_bounds, (float)(g_geom.top * (double)g_scale),
                (float)((g_geom.top + g_geom.height) * (double)g_scale));
}

/* 模糊行：预渲染 → 横向 / 纵向高斯 → 合成。 */

static void draw_row_blurred(Line *line, const Row *row, const Quad *q,
                             Slot *slot, double px_dev, double alpha,
                             double top_edge, double edge, double sigma)
{
    double x0, y0, x1, y1;
    double margin = 3.0 * sigma + 2.0;
    Quad   region;

    quad_bounds(q, &x0, &y0, &x1, &y1);

    region.x = CLAMP(x0 - margin, 0.0, (double)g_view_w);
    region.y = CLAMP(y0 - margin, 0.0, (double)g_view_h);
    region.w = CLAMP(x1 + margin, 0.0, (double)g_view_w) - region.x;
    region.h = CLAMP(y1 + margin, 0.0, (double)g_view_h) - region.y;

    if (region.w < 1.0 || region.h < 1.0)
        return;

    region.ox   = region.x;
    region.oy   = region.y;
    region.sc   = 1.0;

    /* 离屏 pass 关闭混合，避免叠加残留。 */
    glDisable(GL_BLEND);

    /* 清除范围覆盖 ±3σ，避免白边。 */
    {
        double clr = margin * 1.6;
        double cx0 = MAX(0.0, x0 - clr);
        double cy0 = MAX(0.0, y0 - clr);
        double cx1 = MIN((double)g_view_w, x1 + clr);
        double cy1 = MIN((double)g_view_h, y1 + clr);

        glEnable(GL_SCISSOR_TEST);

        for (int i = 0; i < 2; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo[i]);
            glScissor((GLint)floor(cx0), (GLint)floor((double)g_view_h - cy1),
                      (GLsizei)MAX(1.0, ceil(cx1) - floor(cx0)),
                      (GLsizei)MAX(1.0, ceil(cy1) - floor(cy0)));
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        scissor_rect(region.x, region.y, region.x + region.w, region.y + region.h);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo[0]);

    glUseProgram(g_prog_text);
    row_uniforms(row, slot, q, px_dev);
    glUniform1f(u_progress, (float)CLAMP(line->progress, 0.0, 1.0));
    glUniform1f(u_alpha, 1.0f);
    glUniform1f(u_blur, 0.0f);
    glUniform1f(u_lod, 0.0f);
    glUniform2f(u_edge, 0.0f, 0.0f);
    glUniform3f(u_color, (float)line->col.r, (float)line->col.g,
                (float)line->col.b);
    bind_slot(slot);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUseProgram(g_prog_blur);
    glUniform1i(b_tex, 0);
    glUniform2f(b_texel, 1.0f / (float)MAX(g_blur_w, 1),
                1.0f / (float)MAX(g_blur_h, 1));
    glUniform2f(b_uvoff, 0.0f,
                (float)g_view_h / (float)MAX(g_blur_h, 1));
    glUniform1f(b_sigma, (float)sigma);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex[0]);
    glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo[1]);
    glUniform2f(b_dir, 1.0f, 0.0f);
    quad_set(&g_qu_blur, &region);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindTexture(GL_TEXTURE_2D, g_blur_tex[1]);
    glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo[0]);
    glUniform2f(b_dir, 0.0f, 1.0f);
    quad_set(&g_qu_blur, &region);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, g_home_fbo);
    glEnable(GL_BLEND);
    if (g_geom.height > 1.0)
        scissor_lyric();
    glUseProgram(g_prog_comp);
    glUniform1i(c_tex, 0);
    glUniform2f(c_uvscale, 1.0f / (float)MAX(g_blur_w, 1),
                1.0f / (float)MAX(g_blur_h, 1));
    glUniform2f(c_uvoff, 0.0f,
                (float)g_view_h / (float)MAX(g_blur_h, 1));
    glUniform1f(c_alpha, (float)alpha);
    glUniform2f(c_bounds, (float)(g_geom.top * (double)g_scale),
                (float)((g_geom.top + g_geom.height) * (double)g_scale));
    glUniform2f(c_edge, (float)top_edge, (float)edge);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex[0]);
    quad_set(&g_qu_comp, &region);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void draw_row(Line *line, const Row *row)
{
    double px_dev   = MAX(line->lay_px, 1.0) * (double)g_scale;
    double x_dev    = g_geom.x * (double)g_scale;
    double y_dev    = (line->y + row->dy) * (double)g_scale;
    double row_h    = MAX(row->h, line->lay_px) * (double)g_scale;
    double top_dev  = g_geom.top * (double)g_scale;
    double bot_dev  = (g_geom.top + g_geom.height) * (double)g_scale;
    double edge     = g_geom.px * EDGE_FADE_EM * (double)g_scale;
    double top_edge = line->role <= -2 ? edge : 0.0;
    double alpha    = CLAMP(line->alpha, 0.0, 1.0);
    double blur     = CLAMP(line->blur, 0.0, BLUR_MAX) * (double)g_scale;
    double sigma    = blur * BLUR_SIGMA_K;
    double weight   = line->is_current ? WEIGHT_ACTIVE : WEIGHT_IDLE;
    Quad   q;
    Slot  *slot;

    if (row->text == NULL || *row->text == '\0' || alpha <= 0.002)
        return;

    if (y_dev > bot_dev || y_dev + row_h < top_dev)
        return;

    slot = slot_get(row->text, px_dev, weight, row->order, row->n_chars);
    if (slot == NULL || slot->tex == 0)
        return;

    {
        double text_w = (double)slot->w - (double)slot->pad * 2.0;
        double left   = align_left(x_dev, g_geom.w * (double)g_scale, text_w,
                                   &q.ox);

        q.x = left - (double)slot->pad;
    }

    q.y  = y_dev - (double)slot->pad;
    q.w  = (double)slot->w;
    q.h  = (double)slot->h;
    q.oy = (line->y + line->h * 0.5) * (double)g_scale;

    /* 限制放大后不越出歌词区。 */
    q.sc = CLAMP(line->scale, 0.5, 1.5);

    if (line->w_max > 1.0 && line->w_max * q.sc > g_geom.w)
        q.sc = g_geom.w / line->w_max;

    if (blur >= 0.35 && blur_ensure(g_view_w, g_view_h)) {
        draw_row_blurred(line, row, &q, slot, px_dev, alpha, top_edge, edge, sigma);

        glUseProgram(g_prog_text);
        scissor_lyric();
        line->drawn = TRUE;
        return;
    }

    glUseProgram(g_prog_text);
    row_uniforms(row, slot, &q, px_dev);
    glUniform1f(u_progress, (float)CLAMP(line->progress, 0.0, 1.0));
    glUniform1f(u_alpha, (float)alpha);
    glUniform1f(u_blur, (float)blur);
    glUniform1f(u_lod, (float)blur_lod(blur));
    glUniform2f(u_edge, (float)top_edge, (float)edge);
    glUniform3f(u_color, (float)line->col.r, (float)line->col.g,
                (float)line->col.b);
    bind_slot(slot);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    line->drawn = TRUE;
}

static gboolean line_animating(const Line *l)
{
    if (l->text == NULL)
        return FALSE;

    if (l->y_wait > 0.0)
        return TRUE;

    return fabs(l->y_t - l->y) > 0.05 || fabs(l->vy) > 0.8 ||
           fabs(l->alpha_t - l->alpha) > 0.004 ||
           fabs(l->scale_t - l->scale) > 0.0005 || fabs(l->vscale) > 0.02 ||
           fabs(l->blur_t - l->blur) > 0.02 ||
           fabs(l->tint_t - l->tint) > 0.004 ||
           fabs(l->dim_t - l->dim) > 0.004;
}

/* 当前行仍在逐字推进。 */
static gboolean sweeping(void)
{
    if (!g_pos_playing)
        return FALSE;

    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        const Line *l = &g_lines[i];

        if (l->text != NULL && l->idx == g_focus_idx && l->char_ms != NULL &&
            l->progress < 0.999 && l->alpha > 0.05)
            return TRUE;
    }

    return FALSE;
}

static gboolean anything_animating(void)
{
    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        if (line_animating(&g_lines[i]))
            return TRUE;
    }

    return FALSE;
}

static double g_pos_stiff = POS_MIN_STIFF;
static double g_pos_damp  = POS_MIN_STIFF;

static void line_color(Line *l)
{
    double t    = g_geom.accent.a > 0.01 ? l->tint : 0.0;
    Rgba   base = mix_color(col_text, COL_LYRIC_DIM, l->dim);

    l->col = mix_color(base, g_geom.accent, t);
}

static void line_step(Line *l, double dt)
{
    double a;

    if (l->text == NULL)
        return;

    if (l->y_wait > 0.0) {
        l->y_wait -= dt;

        if (l->y_wait <= 0.0) {
            l->y_wait = 0.0;
            l->y_t    = l->y_pending;
        }
    } else {
        l->y_t = l->y_pending;
    }

    a = ((l->scale_t - l->scale) * SCALE_SPRING_STIFF
         - l->vscale * SCALE_SPRING_DAMPING) / SCALE_SPRING_MASS;
    l->vscale += a * dt;
    l->scale  += l->vscale * dt;

    if (fabs(l->scale_t - l->scale) < 0.0005 && fabs(l->vscale) < 0.02) {
        l->scale  = l->scale_t;
        l->vscale = 0.0;
    }

    a = ((l->y_t - l->y) * g_pos_stiff - l->vy * g_pos_damp) / POS_SPRING_MASS;
    l->vy += a * dt;
    l->y  += l->vy * dt;

    if (fabs(l->y_t - l->y) < 0.05 && fabs(l->vy) < 0.8) {
        l->y  = l->y_t;
        l->vy = 0.0;
    }

    l->alpha += (l->alpha_t - l->alpha) * (1.0 - exp(-dt / l->alpha_tau));
    l->blur  += (l->blur_t - l->blur) * (1.0 - exp(-dt / BLUR_RAMP_TAU));

    {
        double tau = l->tint_t > l->tint ? TINT_TAU : l->tint_tau;

        l->tint += (l->tint_t - l->tint) * (1.0 - exp(-dt / MAX(tau, 0.02)));
        l->dim  += (l->dim_t - l->dim)
                 * (1.0 - exp(-dt / MAX(l->tint_tau, 0.02)));
    }

    line_color(l);
}

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    gint64 now = now_us();
    double dt  = g_last_us != 0 ? (double)(now - g_last_us) / 1000000.0 : 1.0 / 60.0;

    (void)clock;
    (void)data;

    g_last_us = now;
    dt = CLAMP(dt, 1.0 / 240.0, 1.0 / 25.0);

    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        line_step(&g_lines[i], dt);

        if (g_lines[i].text != NULL && g_lines[i].idx == g_focus_idx)
            g_lines[i].progress = g_lines[i].char_ms != NULL
                                ? scan_progress(g_lines[i].idx) : 1.0;
    }

    gtk_widget_queue_draw(widget);

    if (sweeping())
        return G_SOURCE_CONTINUE;

    if (!anything_animating()) {
        g_last_us = 0;
        g_tick_id = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void ensure_tick(void)
{
    if (g_tick_id != 0 || g_area == NULL || !gtk_widget_get_mapped(g_area))
        return;

    if (g_dragging)
        return;

    if (!anything_animating() && !sweeping())
        return;

    g_last_us = 0;
    g_tick_id = gtk_widget_add_tick_callback(g_area, on_tick, NULL, NULL);
}

static void on_map(GtkWidget *widget, gpointer data)
{
    (void)data;

    ensure_tick();
    gtk_widget_queue_draw(widget);
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *context, gpointer data)
{
    int    w = gtk_widget_get_width(GTK_WIDGET(area));
    int    h = gtk_widget_get_height(GTK_WIDGET(area));
    int    order[LYRIC_LINE_MAX];
    int    n = 0;

    (void)context;
    (void)data;

    if (!ensure_resources() || w <= 0 || h <= 0)
        return TRUE;

    perf_phase_begin(PERF_PHASE_LYRIC);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_home_fbo);

    g_scale  = gl_util_viewport(GTK_WIDGET(area));
    g_view_w = MAX(1, w * g_scale);
    g_view_h = MAX(1, h * g_scale);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_prog_text);
    glBindVertexArray(g_vao);

    glUniform1f(u_dim, (float)DIM_UNSUN);

    if (g_geom.height > 1.0) {
        glEnable(GL_SCISSOR_TEST);
        scissor_lyric();
    }

    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        if (g_lines[i].text != NULL && g_lines[i].alpha > 0.002)
            order[n++] = i;
    }

    for (int i = 1; i < n; i++) {
        int    cur = order[i];
        double key = g_lines[cur].y;
        int    j   = i - 1;

        while (j >= 0 && g_lines[order[j]].y > key) {
            order[j + 1] = order[j];
            j--;
        }

        order[j + 1] = cur;
    }

    for (int i = 0; i < LYRIC_LINE_MAX; i++)
        g_lines[i].drawn = FALSE;

    for (int i = 0; i < n; i++) {
        Line *line = &g_lines[order[i]];

        glUseProgram(g_prog_text);

        for (int r = 0; r < line->n_rows; r++)
            draw_row(line, &line->rows[r]);
    }

    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, g_home_fbo);

    report_lines();
    perf_phase_end(PERF_PHASE_LYRIC);

    return TRUE;
}

static void measure_setup(void)
{
    if (g_measure_cr != NULL)
        return;

    g_measure_surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    g_measure_cr      = cairo_create(g_measure_surface);
}

static void line_rows_clear(Line *l)
{
    for (int i = 0; i < l->n_rows; i++) {
        g_free(l->rows[i].text);
        g_free(l->rows[i].order);
    }

    g_clear_pointer(&l->rows, g_free);
    l->n_rows  = 0;
    l->h       = 0.0;
    l->w_max   = 0.0;
    l->char_ms = NULL;
    l->n_chars = 0;
}

/* 折行（最多两行），并归一化逐字时间。 */
static void line_rows_build(Line *l, const gchar *text)
{
    TextStyle     style;
    TextRow      *rows = NULL;
    const gint64 *char_ms = NULL;
    gsize         n_chars_total = 0;
    double        start_ms = 0.0, end_ms = 0.0;
    int           n = 0, chars = 0;
    double        h = 0.0;
    double        px = g_geom.px;
    double        weight = l->is_current ? WEIGHT_ACTIVE : WEIGHT_IDLE;

    line_rows_clear(l);

    if (text == NULL || *text == '\0')
        return;

    measure_setup();

    char_ms = line_has_words(l->idx)
        ? lyrics_char_times(g_lyrics, l->idx, &n_chars_total) : NULL;

    line_span(l->idx, &start_ms, &end_ms);

    if (char_ms != NULL && n_chars_total > 0) {
        double last = (double)char_ms[n_chars_total - 1];

        if (last + 120.0 > end_ms)
            end_ms = last + 120.0;
    }

    for (int attempt = 0; attempt < 6; attempt++) {
        style = TEXT_STYLE(FONT_MONO, px, (PangoWeight)weight, px * 0.10,
                           WHITE(1.0));
        rows  = text_wrap(g_measure_cr, text, &style, g_geom.w, &n, &h);

        if (rows == NULL || n <= LYRIC_SLOT_ROWS || px <= g_geom.px * 0.72)
            break;

        g_clear_pointer(&rows, g_free);
        n = 0;
        h = 0.0;
        px *= 0.92;
    }

    if (rows == NULL || n <= 0) {
        g_free(rows);
        return;
    }

    l->rows       = g_new0(Row, (gsize)n);
    l->n_rows     = n;
    l->h          = h > 0.0 ? h : px;
    l->lay_w      = g_geom.w;
    l->lay_px     = px;
    l->lay_weight = weight;
    l->w_max      = 0.0;

    for (int i = 0; i < n; i++) {
        Row *row = &l->rows[i];

        row->text    = g_strndup(text + rows[i].start, rows[i].len);
        row->n_chars = (int)g_utf8_strlen(text + rows[i].start, (gssize)rows[i].len);
        row->dy      = rows[i].y;
        row->h       = MAX(rows[i].h, 1.0);

        if (row->n_chars > 0) {
            row->w   = text_width(g_measure_cr, row->text, &style);
            l->w_max = MAX(l->w_max, row->w);
        }

        if (row->n_chars <= 0) {
            g_clear_pointer(&row->text, g_free);
            chars += row->n_chars;
            continue;
        }

        if (char_ms != NULL && end_ms - start_ms > 1.0) {
            row->order = g_new0(guint8, (gsize)row->n_chars);

            for (int c = 0; c < row->n_chars; c++) {
                gsize  gi = (gsize)chars + (gsize)c;
                double t;

                if (gi >= n_chars_total)
                    break;

                t = ((double)char_ms[gi] - start_ms) / (end_ms - start_ms);
                row->order[c] = (guint8)(255.0 * CLAMP(t, 0.0, 1.0) + 0.5);
            }
        }

        chars += row->n_chars;
    }

    g_free(rows);
}

static gssize neighbour(gssize index, int direction, int steps)
{
    if (g_lyrics == NULL)
        return -1;

    for (gssize i = index + direction;
         i >= 0 && i < (gssize)g_lyrics->n_lines; i += direction) {
        const gchar *text = g_lyrics->lines[i].text;

        if (text != NULL && *text != '\0') {
            if (--steps <= 0)
                return i;
        }
    }

    return -1;
}

/* 0 = 演出；>0 = 预演；<0 = 已唱。 */
static void role_for_distance(int d, double *alpha, double *scale, double *blur,
                              double *tint)
{
    *tint = d == 0 ? 1.0 : 0.0;

    if (d == 0) {
        *alpha = 1.0;
        *scale = SCALE_ACTIVE;
        *blur  = 0.0;
        return;
    }

    if (d == -1) {
        *alpha = PREV_ALPHA;
        *scale = SCALE_PREV;
        *blur  = BLUR_PREV;
        return;
    }

    if (d < -1) {
        *alpha = 0.0;
        *scale = SCALE_INACTIVE2;
        *blur  = BLUR_EXIT;
        return;
    }

    if (d == 1) {
        *alpha = DIM_ALPHA_1;
        *scale = SCALE_INACTIVE;
        *blur  = BLUR_P1;
        return;
    }

    *alpha = CLAMP(DIM_ALPHA_1 * 0.62, 0.18, 1.0);
    *scale = SCALE_INACTIVE2;
    *blur  = BLUR_P2;
}

static Line *line_for_index(gssize idx, gboolean *fresh)
{
    Line *slot = NULL;

    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        if (g_lines[i].text != NULL && g_lines[i].idx == idx) {
            *fresh = FALSE;
            return &g_lines[i];
        }
    }

    for (int i = 0; i < LYRIC_LINE_MAX && slot == NULL; i++) {
        if (g_lines[i].text == NULL)
            slot = &g_lines[i];
    }

    if (slot == NULL) {
        slot = &g_lines[0];

        for (int i = 1; i < LYRIC_LINE_MAX; i++) {
            if (g_lines[i].alpha < slot->alpha)
                slot = &g_lines[i];
        }

        line_rows_clear(slot);
        g_clear_pointer(&slot->text, g_free);
        memset(slot, 0, sizeof(*slot));
    }

    *fresh = TRUE;
    return slot;
}

static double line_weight(const Line *l)
{
    return l->is_current ? (double)WEIGHT_ACTIVE : (double)WEIGHT_IDLE;
}

static Line *line_begin(gssize idx, const gchar *text, int d)
{
    Line    *line;
    gboolean fresh = FALSE;
    double   alpha, scale, blur;

    line = line_for_index(idx, &fresh);

    if (fresh) {
        line->idx       = idx;
        line->vy        = 0.0;
        line->vscale    = 0.0;
        line->role      = d;
        line->alpha_tau = d == 0 ? FADE_TAU : PREVIEW_FADE_TAU;
        line->emerge    = d > 0 ? g_geom.px * EMERGE_EM : 0.0;
        line->alpha     = d == 0 ? 0.45 : 0.0;
        line->scale     = d < 0 ? SCALE_PREV : SCALE_INACTIVE;
        line->blur      = d < 0 ? BLUR_PREV : (d == 0 ? 0.0 : BLUR_P1);
        line->dim       = d == 0 ? 0.0 : 1.0;
    }

    line->role       = d;
    line->is_current = d == 0;

    if (line->text == NULL || strcmp(line->text, text) != 0 ||
        line->lay_w != g_geom.w || line->lay_px != g_geom.px ||
        line->n_rows == 0 || line->lay_weight != line_weight(line)) {
        g_free(line->text);
        line->text = g_strdup(text);
        line_rows_build(line, text);
    }

    role_for_distance(d, &alpha, &scale, &blur, &line->tint_t);

    line->alpha_t = alpha;
    line->scale_t = scale;
    line->blur_t  = blur;
    line->dim_t   = d == 0 ? 0.0 : 1.0;
    line->progress = d == 0 && line->char_ms != NULL ? scan_progress(idx) : 1.0;
    line->char_ms  = line_has_words(idx)
        ? g_lyrics->lines[idx].char_ms : NULL;
    line->n_chars  = line_has_words(idx) ? g_lyrics->lines[idx].n_chars : 0;
    line->alive      = TRUE;

    line->tint_tau = d < 0 ? TINT_OUT_TAU : TINT_TAU;

    line->y_delay = STAGGER_SEC * (double)MAX(d, 0);

    line_color(line);

    return line;
}

static void line_place(Line *l, double y, gboolean snap)
{
    if (!snap && l->placed && fabs(y - l->y_pending) > 0.01) {
        l->y_pending = y;
        l->y_wait    = l->y_delay;
    }

    if (snap || !l->placed) {
        l->y         = y + (snap ? 0.0 : l->emerge);
        l->vy        = 0.0;
        l->y_t       = y;
        l->y_pending = y;
        l->y_wait    = 0.0;
        l->placed    = TRUE;
        l->emerge    = 0.0;
    }
}

static void spring_policy(gssize index)
{
    double start = 0.0, end = 0.0, interval, ci, ratio;

    if (g_lyrics == NULL || index < 0 || index >= (gssize)g_lyrics->n_lines) {
        g_pos_stiff = POS_SLOW_STIFF;
        g_pos_damp  = POS_SLOW_DAMP;
        return;
    }

    line_span(index, &start, &end);
    interval = end - start;

    if (interval <= 1.0) {
        g_pos_stiff = POS_SLOW_STIFF;
        g_pos_damp  = POS_SLOW_DAMP;
        return;
    }

    ci    = CLAMP(interval, POS_MIN_INTERVAL, POS_MAX_INTERVAL);
    ratio = pow(1.0 - (ci - POS_MIN_INTERVAL) / (POS_MAX_INTERVAL - POS_MIN_INTERVAL),
                POS_INTERVAL_EXP);

    g_pos_stiff = POS_MIN_STIFF + ratio * (POS_MAX_STIFF - POS_MIN_STIFF);
    g_pos_damp  = sqrt(g_pos_stiff) * POS_DAMP_MULT;
}

void lyricgl_set_view(const Lyrics *lyrics, gssize index, const LyricViewGeom *geom)
{
    gboolean snap;

    if (g_area == NULL || geom == NULL)
        return;

    snap = (g_index < 0 || index < 0 || labs((long)(index - g_index)) > SNAP_JUMP);

    g_lyrics = lyrics;
    g_index  = index;

    /* 抑制 ±1px 抖动引起的重新排版。 */
    if (g_geom.height > 1.0) {
        LyricViewGeom g = *geom;

        if (fabs(g.w - g_geom.w) < 1.0)
            g.w = g_geom.w;

        if (fabs(g.height - g_geom.height) < 1.0)
            g.height = g_geom.height;

        if (fabs(g.top - g_geom.top) < 1.0)
            g.top = g_geom.top;

        if (fabs(g.px - g_geom.px) < 0.05)
            g.px = g_geom.px;

        g_geom = g;
    } else {
        g_geom = *geom;
    }
    g_pos_ms      = geom->position_ms;
    g_pos_us      = now_us();
    g_pos_playing = geom->playing;
    g_anchor_no_focus = FALSE;

    for (int i = 0; i < LYRIC_LINE_MAX; i++)
        g_lines[i].alive = FALSE;

    if (index < 0 && lyrics != NULL && lyrics->n_lines > 0) {
        for (gsize i = 0; i < lyrics->n_lines; i++) {
            const gchar *text = lyrics->lines[i].text;

            if (text != NULL && *text != '\0') {
                index = (gssize)i;
                g_anchor_no_focus = TRUE;
                break;
            }
        }
    }

    if (lyrics == NULL || index < 0 || index >= (gssize)lyrics->n_lines ||
        geom->height <= 1.0) {
        g_focus_idx = -1;

        for (int i = 0; i < LYRIC_LINE_MAX; i++) {
            if (g_lines[i].text != NULL) {
                g_lines[i].alpha_t = 0.0;
                g_lines[i].alive   = FALSE;
            }
        }

        gtk_widget_queue_draw(g_area);
        ensure_tick();
        return;
    }

    g_focus_idx = g_anchor_no_focus ? -1 : index;

    spring_policy(g_focus_idx);

    double pitch = lyric_slot_h(geom->px) + geom->gap;

    {
        Line  *stack[ROW_MAX];
        int    n = 0, cur_i = 0;
        gssize idx = index;
        double slot, gap, top_off, y, delay, base;

        if (!g_anchor_no_focus) {
            gssize prev = neighbour(index, -1, 1);

            if (prev >= 0) {
                const gchar *ptext = lyrics->lines[prev].text;

                if (ptext != NULL && *ptext != '\0') {
                    stack[n++] = line_begin(prev, ptext, -1);
                    cur_i      = 1;
                }
            }
        }

        stack[n++] = line_begin(index, lyrics->lines[index].text,
                                g_anchor_no_focus ? 1 : 0);

        for (int d = 1; d <= STACK_MAX && n < ROW_MAX; d++) {
            const gchar *text;

            idx = neighbour(idx, +1, 1);

            if (idx < 0)
                break;

            text = lyrics->lines[idx].text;

            if (text == NULL || *text == 0)
                continue;

            stack[n++] = line_begin(idx, text, g_anchor_no_focus ? d + 1 : d);
        }

        slot = lyric_slot_h(geom->px);

        for (int i = 0; i < n; i++)
            slot = MAX(slot, stack[i]->h);

        gap = geom->gap;

        {
            double room = (geom->height - (double)LYRIC_SLOT_N * slot)
                        / (double)(LYRIC_SLOT_N - 1);

            gap = CLAMP(MIN(room, geom->gap * GAP_MAX_K),
                        geom->px * 0.18, geom->gap * GAP_MAX_K);
        }

        pitch = slot + gap;

        {
            double total = (double)LYRIC_SLOT_N * slot
                         + (double)(LYRIC_SLOT_N - 1) * gap;

            if (total > geom->height && total > 1.0) {
                double k = geom->height / total;

                slot *= k;
                gap  *= k;
                pitch = slot + gap;
            }
        }

        top_off = (geom->height - ((double)LYRIC_SLOT_N * slot
                                  + (double)(LYRIC_SLOT_N - 1) * gap)) * 0.5;
        top_off = CLAMP(top_off, 0.0, geom->px * 1.2);

        delay = 0.0;
        base  = STAGGER_SEC;
        y     = geom->top + top_off;

        for (int i = 0; i < n; i++) {
            int    slot_i = 1 + i - cur_i;
            double ty     = y + (double)slot_i * pitch
                          + (slot - stack[i]->h) * 0.5;

            stack[i]->y_delay = delay;
            delay += base;

            if (i >= cur_i)
                base *= STAGGER_DECAY;

            line_place(stack[i], ty, snap);
        }
    }

    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        Line *l = &g_lines[i];
        double alpha, scale, blur, tint;

        if (l->text == NULL || l->alive)
            continue;

        if (!snap && l->role == -1) {
            role_for_distance(-2, &alpha, &scale, &blur, &tint);

            l->role    = -2;
            l->alpha_t = alpha;
            l->scale_t = scale;
            l->blur_t  = blur;
            l->tint_t  = tint;
            l->dim_t   = 1.0;
            l->tint_tau = TINT_OUT_TAU;

            l->y_delay = 0.0;
            line_place(l, l->y - pitch, FALSE);
            continue;
        }

        l->alpha_t = 0.0;
        l->blur_t  = 0.0;
        l->tint_t  = 0.0;
        l->role    = -2;
    }

    if (snap) {
        for (int i = 0; i < LYRIC_LINE_MAX; i++) {
            Line *l = &g_lines[i];

            if (l->text == NULL)
                continue;

            l->y        = l->y_t;
            l->vy       = 0.0;
            l->scale    = l->scale_t;
            l->vscale   = 0.0;
            l->blur     = l->blur_t;
            l->alpha    = l->alpha_t;
            l->tint     = l->tint_t;
            l->dim      = l->dim_t;
            line_color(l);
        }
    }

    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        if (g_lines[i].text != NULL && g_lines[i].alpha <= 0.003 &&
            g_lines[i].alpha_t <= 0.0) {
            line_rows_clear(&g_lines[i]);
            g_clear_pointer(&g_lines[i].text, g_free);
            memset(&g_lines[i], 0, sizeof(g_lines[i]));
        }
    }

    gtk_widget_queue_draw(g_area);
    ensure_tick();
}

void lyricgl_drag_state(gboolean dragging)
{
    g_dragging = dragging;

    if (dragging) {
        if (g_tick_id != 0) {
            gtk_widget_remove_tick_callback(g_area, g_tick_id);
            g_tick_id = 0;
        }

        return;
    }

    ensure_tick();

    if (g_area != NULL)
        gtk_widget_queue_draw(g_area);
}

void lyricgl_clear(void)
{
    for (int i = 0; i < LYRIC_LINE_MAX; i++) {
        line_rows_clear(&g_lines[i]);
        g_clear_pointer(&g_lines[i].text, g_free);
        memset(&g_lines[i], 0, sizeof(g_lines[i]));
    }

    g_lyrics          = NULL;
    g_index           = -1;
    g_focus_idx       = -1;
    g_anchor_no_focus = FALSE;

    if (uidbg_enabled())
        uidbg_layer_clear(UIDBG_LAYER_LYRIC);

    if (g_area != NULL)
        gtk_widget_queue_draw(g_area);
}

static void on_realize(GtkWidget *widget, gpointer data)
{
    (void)data;

    gtk_gl_area_make_current(GTK_GL_AREA(widget));
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_home_fbo);
    ensure_resources();
}

static void on_unrealize(GtkWidget *widget, gpointer data)
{
    (void)data;

    if (g_tick_id != 0) {
        gtk_widget_remove_tick_callback(widget, g_tick_id);
        g_tick_id = 0;
    }

    for (int i = 0; i < SLOT_MAX; i++) {
        g_free(g_slots[i].key);
        memset(&g_slots[i], 0, sizeof(g_slots[i]));
    }

    blur_free();

    g_prog_text = 0;
    g_prog_blur = 0;
    g_prog_comp = 0;
    g_vao       = 0;
    g_vbo       = 0;
    g_ready     = FALSE;
    g_blur_ok   = FALSE;
    g_last_us   = 0;
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    g_area    = NULL;
    g_ready   = FALSE;
    g_tick_id = 0;
    g_lyrics  = NULL;
    g_index   = -1;

    if (g_measure_cr != NULL) {
        cairo_destroy(g_measure_cr);
        g_measure_cr = NULL;
    }

    if (g_measure_surface != NULL) {
        cairo_surface_destroy(g_measure_surface);
        g_measure_surface = NULL;
    }

    lyricgl_clear();
}

GtkWidget *lyricgl_new(void)
{
    GtkWidget *area;

    if (g_strcmp0(g_getenv("FOLLOW_LYRICGL"), "0") == 0) {
        g_failed = TRUE;
        return NULL;
    }

    area = gtk_gl_area_new();

    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_widget_set_can_target(area, FALSE);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(area), FALSE);
    gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(area), FALSE);
    gtk_gl_area_set_required_version(GTK_GL_AREA(area), 3, 3);

#if GTK_CHECK_VERSION(4, 6, 0)
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area), GDK_GL_API_GL);
#endif

    g_signal_connect(area, "render", G_CALLBACK(on_render), NULL);
    g_signal_connect(area, "map", G_CALLBACK(on_map), NULL);
    g_signal_connect_after(area, "realize", G_CALLBACK(on_realize), NULL);
    g_signal_connect(area, "unrealize", G_CALLBACK(on_unrealize), NULL);
    g_signal_connect(area, "destroy", G_CALLBACK(on_destroy), NULL);

    g_area = area;

    return area;
}

gboolean lyricgl_ready(void)
{
    return g_ready;
}
