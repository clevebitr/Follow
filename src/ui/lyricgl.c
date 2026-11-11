/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#include "lyricgl.h"

#include <epoxy/gl.h>
#include <math.h>
#include <string.h>

#include "glutil.h"
#include "theme.h"
#include "uidbg.h"

/* ---------------- 参数（照 AMLL 抄的） ---------------- */

/* 行位置的弹簧：AMLL posYSpringParams 原值 0.9 / 15 / 90，
 * 这里加硬一点（同一个阻尼比 ≈0.82），上滑补位更利索 */
#define POS_SPRING_MASS      0.9
#define POS_SPRING_DAMPING  17.0
#define POS_SPRING_STIFF   120.0
/* 每往下一行，起跑延迟多这么多秒：上滑时上下行的间隙会先拉开再收回 */
#define STAGGER_SEC         0.05
/* 缩放的弹簧：AMLL scaleSpringParams（阻尼比 ~0.88，带一点回弹） */
#define SCALE_SPRING_MASS      2.0
#define SCALE_SPRING_DAMPING  25.0
#define SCALE_SPRING_STIFF   100.0

/* 缩放：当前行放大、其余行缩小（AMLL 是 100 / 97，这里把差距拉大一点，
 * 让“正在演出的那一行”一眼能看出来） */
#define SCALE_ACTIVE      1.12
#define SCALE_INACTIVE    0.94   /* 第一行预显示 */
#define SCALE_INACTIVE2   0.90   /* 第二行预显示：越远越小，别和上一行撞成一片 */
#define SCALE_PREV        0.95   /* 预推出列（刚唱完、还挂在上面那句） */

/* 模糊等级（逻辑像素），越靠下越糊：
 *   预推出列   1.0
 *   预演第一行 1.4（比原来 2.0 轻：刚离开演出位的下一句还应该看得清）
 *   预演第二行 4.2（比原来 3.2 重：拉到背景里去） */
#define BLUR_PREV         1.0
#define BLUR_P1           1.4
#define BLUR_P2           4.2
#define BLUR_MAX          5.0
/* AMLL 的 .lyricLineWrapper 是 filter 0.4s ease，这里用指数收敛近似 */
#define BLUR_TAU          0.11
/* 透明度收敛 */
#define FADE_TAU          0.09
/* 预演行淡入 / 入场：新行从下方“浮”上来，别在底部凭空冒出来 */
#define PREVIEW_FADE_TAU  0.22
#define EMERGE_EM         1.6

/* 当前行还没唱到的字压到多暗（只有逐字歌词才用得上） */
#define DIM_UNSUN         0.45
/* 扫描头渐变宽度 / 字号（AMLL 的 wordFadeWidth = 0.5） */
#define SWEEP_EM          0.5

/* 区域底部渐隐的高度 / 字号 */
#define EDGE_FADE_EM      0.7

/* 唱完的当前行：先缩小，再滑出去 */
#define EXIT_SCALE        0.90
#define EXIT_SHRINK_SEC   0.09
#define EXIT_SLIDE_DELAY  0.03
#define EXIT_SLIDE_SEC    0.17
#define EXIT_TOTAL_SEC    (EXIT_SLIDE_DELAY + EXIT_SLIDE_SEC)
#define EXIT_BLUR         2.4   /* 退场时再加重一点模糊（逻辑像素） */

/* 待演出行变成当前行：先在原地缩一下（PROMOTE_DIP），
 * 然后一边上滑一边放大到演出倍率，带一点回弹（apple 味）。 */
#define PROMOTE_DIP       0.88
#define PROMOTE_SHRINK    0.10
#define PROMOTE_RISE      0.28

/* 当前行上方给“正在退场的那一行”预留的高度（按字号算）：
 * 区域里从上到下 = 退场行 / 当前行 / 待演出行，三行各就各位。 */
#define PREV_SLOT_EM      1.15

/* 一次跳很远（seek / 换曲）时不做弹簧，直接落到目标位置 */
#define SNAP_JUMP         3

/* 同一时刻内存里最多保留几行（当前行 + 预显示 + 正在退场的） */
#define LINE_MAX          8
/* 预显示：当前行下面铺两行（上滑补位时下面还有一句顶着） */
#define STACK_MAX         2
/* 版式（从上到下）：预推出列 / 当前句 / 预演一 / 预演二 */
#define ROW_MAX           (STACK_MAX + 2)

/* 字形遮罩四周余量（设备像素）：模糊会把边缘拉出字形，留够才不会切边 */
#define ROW_PAD           16
/* 字形纹理缓存条数 */
#define SLOT_MAX          32

/* 透明度：预推出列 / 预演第一行（AMLL 那行的 opacity 基本是 1，
 * 靠模糊拉开层次；这里保留淡出，让当前行更突出） */
#define PREV_ALPHA        0.58
#define DIM_ALPHA_1       0.72

/* ---------------- shader ---------------- */

/* 单位四边形 -> u_rect（设备像素，左上原点），可绕 u_origin 缩放 */
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

/* 一行字：
 *   u_tex      字形遮罩（带 mipmap：u_lod 就是模糊半径）
 *   u_order    每个字的时间坐标（0..1）；整行高亮的行全是 0
 *   u_progress 当前行唱到哪了（0..1）
 *   u_sweep    扫描头渐变宽度（order 单位）
 *   u_dim      没唱到的字的亮度
 *   u_edge     (上渐隐, 下渐隐) 的高度：只有退场的行才给上渐隐 */
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

/* ---------------- 状态 ---------------- */

/* 一个视觉行（一句歌词折出来的一段） */
typedef struct {
    gchar  *text;      /* 这一段的文字 */
    int     n_chars;   /* 这段几个字 */
    double  dy;        /* 相对整句顶部的偏移（逻辑像素） */
    double  h;         /* 行高（逻辑像素） */
    guint8 *order;     /* 逐字时间坐标 0..255（NULL = 整行一起亮） */
} Row;

typedef struct {
    gssize  idx;          /* 歌词行号 */
    gchar  *text;         /* 整句原文 */
    Row    *rows;
    int     n_rows;
    double  h;            /* 整句折行后的总高度 */
    double  lay_w, lay_px;/* 上次排版的宽度 / 字号 */
    gint64 *char_ms;      /* 整句的逐字时间轴（借用 Lyrics，NULL = 整行高亮） */
    gsize   n_chars;

    /* 目标值 */
    double  y_t, alpha_t, scale_t, blur_t;
    /* y 的起跑延迟：上下行错开一点，上滑时间隙会先拉开再收回 */
    double  y_pending, y_wait, y_delay;
    /* 当前值：y / scale 用弹簧，alpha / blur 指数收敛 */
    double  y, vy, alpha, scale, vscale, blur;
    double  progress;     /* 这行唱到哪了（0..1；非当前行恒为 1） */

    double  exit_t;       /* >= 0：正在退场（先缩小再滑出） */
    double  exit_slide;   /* 退场时向上滑出的距离（逻辑像素） */
    double  promote_t;    /* >= 0：正在从“待演出”升上来（先缩再放大） */
    double  alpha_tau;    /* 透明度收敛时间常数（预演行慢一点） */
    double  emerge;       /* 新行入场时从下方浮上来的距离 */
    double  y_want;       /* set_view 算出来的最新目标 y */
    int     last_d;       /* 上一帧的远近序号（用来发现“被提拔”） */

    gboolean placed;
    gboolean alive;
    gboolean leave_row;   /* 这一帧站在“预推出列”（下一帧就该滑走了） */
    gboolean is_current;  /* 这一行是正在演出的那句（用高亮色） */
    gboolean drawn;       /* 本帧真的发出了 glDrawArrays（uidbg 只报画出来的） */
} Line;

static Line g_lines[LINE_MAX];

/* 视图参数 */
static const Lyrics *g_lyrics;
static gssize  g_index = -1;
static gssize  g_focus_idx = -1;
static gssize  g_logged_index = -2;
static gboolean g_anchor_no_focus;
static LyricViewGeom g_geom;
static int     g_scale = 1;

static cairo_surface_t *g_measure_surface;
static cairo_t         *g_measure_cr;

static GtkWidget *g_area;
static gboolean   g_ready;
static gboolean   g_failed;

static GLuint g_prog_text;
static GLuint g_vao;
static GLuint g_vbo;

static GLint u_rect, u_area, u_origin, u_scale_u, u_tex, u_order, u_color, u_alpha;
static GLint u_progress, u_sweep, u_dim, u_lod, u_blur, u_texel, u_bounds, u_edge;

/* GTK 自己的 framebuffer */
static GLint g_home_fbo;

/* 播放头：视图更新时的位置 + 时间戳，tick 里线性外推，扫描头才能连续走 */
static double   g_pos_ms;
static gint64   g_pos_us;
static gboolean g_pos_playing;

static guint  g_tick_id;
static gint64 g_last_us;

static gint64 now_us(void)
{
    return g_get_monotonic_time();
}

/* 当前播放位置（毫秒） */
static double playhead_ms(void)
{
    double pos = g_pos_ms;

    if (g_pos_playing && g_pos_us != 0)
        pos += (double)(now_us() - g_pos_us) / 1000.0;

    return pos;
}

/* 一行的起止时间：到下一行的时间戳为止（最后一行按 4 秒算） */
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

/* 这行唱到哪了：0 = 一个字没唱，1 = 整行唱完 */
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

/* 行有没有逐字时间轴（没有就只能整行一起亮） */
static gboolean line_has_words(gssize idx)
{
    return g_lyrics != NULL && idx >= 0 && idx < (gssize)g_lyrics->n_lines &&
           g_lyrics->lines[idx].char_ms != NULL;
}

/* ---------------- 缓动 ---------------- */

static double ease_out_cubic(double t)
{
    double u = 1.0 - CLAMP(t, 0.0, 1.0);

    return 1.0 - u * u * u;
}

/* 末端轻微过冲的缓出（0 → 1，峰值约 1.1），放大时用 */
static double ease_out_back(double t)
{
    const double c1 = 1.70158;
    const double c3 = c1 + 1.0;
    double       u  = CLAMP(t, 0.0, 1.0) - 1.0;

    return 1.0 + c3 * u * u * u + c1 * u * u;
}

static double ease_in_out(double t)
{
    t = CLAMP(t, 0.0, 1.0);

    return t < 0.5 ? 4.0 * t * t * t
                   : 1.0 - pow(-2.0 * t + 2.0, 3.0) * 0.5;
}

/* ---------------- GL 资源 ---------------- */

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
    u_scale_u  = glGetUniformLocation(g_prog_text, "u_scale");
    u_tex      = glGetUniformLocation(g_prog_text, "u_tex");
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

    g_ready = TRUE;
    gl_util_log_context("lyricgl");
    return TRUE;
}

/* ---------------- 字形纹理 ---------------- */

typedef struct {
    gchar  *key;
    int     w, h;
    int     pad;
    GLuint  tex;        /* 字形遮罩（带 mipmap，模糊靠 LOD） */
    GLuint  order;      /* 逐字时间坐标（R8，和 tex 对齐） */
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
                           const guint8 *order, int n_order)
{
    TextStyle        style;
    cairo_surface_t *glyph = NULL;
    cairo_surface_t *ord   = NULL;
    int              w = 0, h = 0;

    style = TEXT_STYLE(FONT_MONO, px_dev, PANGO_WEIGHT_BOLD, px_dev * 0.10,
                       WHITE(1.0));

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

static Slot *slot_get(const gchar *text, double px_dev,
                      const guint8 *order, int n_order)
{
    g_autofree gchar *key = g_strdup_printf("%s|%.2f|%08x",
                                            text, px_dev, hash_order(order, n_order));
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

    if (!slot_build(free_slot, text, px_dev, order, n_order)) {
        slot_free(free_slot);
        return NULL;
    }

    free_slot->key  = g_steal_pointer(&key);
    free_slot->used = ++g_slot_clock;

    return free_slot;
}

/* ---------------- 绘制 ---------------- */

/* 扫描头渐变宽（order 单位）：把"0.5 行高"换算成这段文字占的 order 区间 */
static double sweep_for(const Row *row, const Slot *slot, double px_dev)
{
    double text_w = MAX((double)slot->w - (double)(slot->pad * 2), 1.0);
    double fade   = SWEEP_EM * px_dev;

    (void)row;

    return CLAMP(fade / text_w, 0.005, 0.5);
}

/* 模糊半径（设备像素）→ mipmap LOD：
 * 一级 mip 是 2x2 的盒式平均，σ 大致是 2^k 的一半，所以按 3 倍半径折一下。 */
static double blur_lod(double blur_dev)
{
    if (blur_dev <= 0.05)
        return 0.0;

    return CLAMP(log2(1.0 + 3.0 * blur_dev), 0.0, 3.5);
}

/* GL 层的内容进不了 GTK 的截图，cairo 埋点也摸不到：把这次真正画出来的行按
 * 真实几何报给 uidbg，agent 用 {cmd:"screen"} 就能看到歌词层的字 / 位置 / 透明度。 */
static void report_lines(void)
{
    if (!uidbg_enabled())
        return;

    uidbg_begin_layer(UIDBG_LAYER_LYRIC);
    uidbg_layer_clear(UIDBG_LAYER_LYRIC);

    uidbg_record_rect(g_geom.x, g_geom.top, g_geom.w, g_geom.height,
                      col_card, "lyric-area");

    if (g_strcmp0(g_getenv("FOLLOW_LYRICGL_DEBUG"), "1") == 0) {
        g_message("lyricgl: area y=%.1f h=%.1f px=%.1f", g_geom.top,
                  g_geom.height, g_geom.px);

        for (int i = 0; i < LINE_MAX; i++) {
            const Line *l = &g_lines[i];

            if (l->text == NULL)
                continue;

            g_message("lyricgl: idx=%d y=%.1f/%.1f a=%.2f/%.2f s=%.3f/%.3f b=%.2f/%.2f exit=%.2f alive=%d drawn=%d | %.12s",
                      (int)l->idx, l->y, l->y_t, l->alpha, l->alpha_t,
                      l->scale, l->scale_t, l->blur, l->blur_t, l->exit_t,
                      l->alive, l->drawn, l->text);
        }
    }

    for (int i = 0; i < LINE_MAX; i++) {
        const Line *l = &g_lines[i];

        if (l->text == NULL || !l->drawn)
            continue;

        for (int r = 0; r < l->n_rows; r++) {
            const Row *row = &l->rows[r];
            double     y   = l->y + row->dy;

            if (row->text == NULL)
                continue;

            if (y + row->h < g_geom.top ||
                y > g_geom.top + g_geom.height)
                continue;

            uidbg_record_text(g_geom.x, y, g_geom.w, row->h, g_geom.px,
                              WHITE(CLAMP(l->alpha, 0.0, 1.0)), row->text);
        }
    }

    uidbg_begin_layer(UIDBG_LAYER_STAGE);
}

static void draw_row(Line *line, const Row *row)
{
    double x_dev   = g_geom.x * (double)g_scale;
    double y_dev   = (line->y + row->dy) * (double)g_scale;
    double px_dev  = g_geom.px * (double)g_scale;
    double row_h   = MAX(row->h, g_geom.px) * (double)g_scale;
    double bottom  = (g_geom.top + g_geom.height) * (double)g_scale;
    double origin_y = (line->y + line->h * 0.5) * (double)g_scale;
    double edge     = g_geom.px * EDGE_FADE_EM * (double)g_scale;
    double top_edge = line->exit_t >= 0.0 ? edge : 0.0;
    Slot  *slot;

    if (row->text == NULL || *row->text == '\0')
        return;

    /* 区域外的行不建纹理也不画（滚动时滑出底部的那几行） */
    if (y_dev > bottom || y_dev + row_h < g_geom.top * (double)g_scale)
        return;

    slot = slot_get(row->text, px_dev, row->order, row->n_chars);
    if (slot == NULL || slot->tex == 0)
        return;

    /* 正在演出的那句用封面种子色当高亮色，其它行维持白字 */
    {
        Rgba col = (line->is_current && g_geom.accent.a > 0.01)
                   ? g_geom.accent : col_text;

        glUniform3f(u_color, (float)col.r, (float)col.g, (float)col.b);
    }

    glUniform4f(u_rect,
                (float)(x_dev - (double)slot->pad),
                (float)(y_dev - (double)slot->pad),
                (float)slot->w, (float)slot->h);
    /* 放大后别冲出歌词区（右边是封面 / 二维码那一列） */
    {
        double text_w = (double)MAX(slot->w - slot->pad * 2, 1) / (double)g_scale;
        double sc     = CLAMP(line->scale, 0.5, 1.5);

        if (text_w * sc > g_geom.w)
            sc = g_geom.w / text_w;

        glUniform2f(u_origin, (float)x_dev, (float)origin_y);
        glUniform1f(u_scale_u, (float)sc);
    }
    glUniform1f(u_alpha, (float)CLAMP(line->alpha, 0.0, 1.0));
    glUniform1f(u_progress, (float)CLAMP(line->progress, 0.0, 1.0));
    glUniform1f(u_sweep, (float)sweep_for(row, slot, px_dev));
    glUniform1f(u_blur, (float)(line->blur * (double)g_scale));
    glUniform1f(u_lod, (float)blur_lod(line->blur * (double)g_scale));
    glUniform2f(u_texel, 1.0f / (float)MAX(slot->w, 1),
                1.0f / (float)MAX(slot->h, 1));
    glUniform2f(u_edge, (float)top_edge, (float)edge);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, slot->tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, slot->order != 0 ? slot->order : slot->tex);
    glActiveTexture(GL_TEXTURE0);

    glUniform1i(u_tex, 0);
    glUniform1i(u_order, 1);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    line->drawn = TRUE;
}

/* ---------------- 动画 ---------------- */

static gboolean line_animating(const Line *l)
{
    if (l->text == NULL)
        return FALSE;

    if (l->exit_t >= 0.0 && l->exit_t < EXIT_TOTAL_SEC)
        return TRUE;

    if (l->promote_t >= 0.0 && l->promote_t < PROMOTE_SHRINK + PROMOTE_RISE)
        return TRUE;

    if (l->y_wait > 0.0)
        return TRUE;

    return fabs(l->y_t - l->y) > 0.05 || fabs(l->vy) > 0.8 ||
           fabs(l->alpha_t - l->alpha) > 0.004 ||
           fabs(l->scale_t - l->scale) > 0.0005 || fabs(l->vscale) > 0.02 ||
           fabs(l->blur_t - l->blur) > 0.02;
}

/* 当前行还在逐字唱：扫描头要继续推（暂停 / 整行高亮的行都不需要） */
static gboolean sweeping(void)
{
    if (!g_pos_playing)
        return FALSE;

    for (int i = 0; i < LINE_MAX; i++) {
        const Line *l = &g_lines[i];

        if (l->text != NULL && l->idx == g_focus_idx && l->char_ms != NULL &&
            l->progress < 0.999 && l->alpha > 0.05)
            return TRUE;
    }

    return FALSE;
}

static gboolean anything_animating(void)
{
    for (int i = 0; i < LINE_MAX; i++) {
        if (line_animating(&g_lines[i]))
            return TRUE;
    }

    return FALSE;
}

static void line_step(Line *l, double dt)
{
    double a;

    if (l->text == NULL)
        return;

    /* 退场：先缩小并蒙上模糊，然后尽快向上滑出一行 + 淡出
     * （要快，别占着位置挡住正在上台的下一句） */
    if (l->exit_t >= 0.0) {
        double shrink = ease_out_cubic(l->exit_t / EXIT_SHRINK_SEC);
        double slide  = ease_in_out((l->exit_t - EXIT_SLIDE_DELAY) / EXIT_SLIDE_SEC);

        l->scale  = 1.0 - (1.0 - EXIT_SCALE) * shrink;
        l->vscale = 0.0;
        l->y      = l->y_t - l->exit_slide * slide;
        l->vy     = 0.0;
        l->alpha  = 1.0 - slide;
        l->blur   = EXIT_BLUR * CLAMP(shrink * 1.4, 0.0, 1.0);

        l->exit_t += dt;

        if (l->exit_t >= EXIT_TOTAL_SEC) {
            l->exit_t  = -1.0;
            l->alpha   = 0.0;
            l->alpha_t = 0.0;
        }

        return;
    }

    /* 延迟起跑：目标变了之后先等一会儿再走（上台那一行等“缩一下”演完） */
    if (l->y_wait > 0.0) {
        l->y_wait -= dt;

        if (l->y_wait <= 0.0) {
            l->y_wait = 0.0;
            l->y_t    = l->y_pending;
        }
    } else {
        l->y_t = l->y_pending;
    }

    /* 上台（原来在下面的待演出行变成了当前行）：
     * 先原地缩一下（PROMOTE_SHRINK），再上滑 + 放大到演出倍率（带一点回弹）。 */
    if (l->promote_t >= 0.0) {
        double t = l->promote_t;

        if (t < PROMOTE_SHRINK) {
            l->scale = SCALE_INACTIVE
                     + (PROMOTE_DIP - SCALE_INACTIVE) * ease_out_cubic(t / PROMOTE_SHRINK);
            l->blur  = l->blur_t * (1.0 - 0.25 * ease_out_cubic(t / PROMOTE_SHRINK));
        } else {
            double k = ease_out_back((t - PROMOTE_SHRINK) / PROMOTE_RISE);

            l->scale = PROMOTE_DIP + (SCALE_ACTIVE - PROMOTE_DIP) * k;
            l->blur  = l->blur_t * (1.0 - ease_out_cubic((t - PROMOTE_SHRINK) / PROMOTE_RISE));
        }

        l->vscale  = 0.0;
        l->promote_t += dt;

        if (l->promote_t >= PROMOTE_SHRINK + PROMOTE_RISE) {
            l->promote_t = -1.0;
            l->scale     = SCALE_ACTIVE;
            l->blur      = 0.0;
        }
    } else {
        /* scale：AMLL 的缩放弹簧（其余行缩小，当前行放大） */
        a = ((l->scale_t - l->scale) * SCALE_SPRING_STIFF
             - l->vscale * SCALE_SPRING_DAMPING) / SCALE_SPRING_MASS;
        l->vscale += a * dt;
        l->scale  += l->vscale * dt;

        if (fabs(l->scale_t - l->scale) < 0.0005 && fabs(l->vscale) < 0.02) {
            l->scale  = l->scale_t;
            l->vscale = 0.0;
        }
    }

    /* y：半隐式欧拉的弹簧（AMLL solveSpring 同一条方程） */
    a = ((l->y_t - l->y) * POS_SPRING_STIFF - l->vy * POS_SPRING_DAMPING)
        / POS_SPRING_MASS;
    l->vy += a * dt;
    l->y  += l->vy * dt;

    if (fabs(l->y_t - l->y) < 0.05 && fabs(l->vy) < 0.8) {
        l->y  = l->y_t;
        l->vy = 0.0;
    }

    l->alpha += (l->alpha_t - l->alpha) * (1.0 - exp(-dt / l->alpha_tau));
    /* blur 用同一套指数曲线，稍微慢一点（AMLL 是 filter 0.4s ease） */
    l->blur  += (l->blur_t - l->blur) * (1.0 - exp(-dt / BLUR_TAU));
}

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    gint64 now = now_us();
    double dt  = g_last_us != 0 ? (double)(now - g_last_us) / 1000000.0 : 1.0 / 60.0;

    (void)clock;
    (void)data;

    g_last_us = now;
    dt = CLAMP(dt, 1.0 / 240.0, 1.0 / 25.0);

    for (int i = 0; i < LINE_MAX; i++) {
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
    int    order[LINE_MAX];
    int    n = 0;
    double top_dev, bot_dev;

    (void)context;
    (void)data;

    if (!ensure_resources() || w <= 0 || h <= 0)
        return TRUE;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_home_fbo);

    g_scale = gl_util_viewport(GTK_WIDGET(area));
    top_dev = g_geom.top * (double)g_scale;
    bot_dev = (g_geom.top + g_geom.height) * (double)g_scale;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    /* 预乘 alpha：字叠在这层透明画布上，再由 GTK 合成到背景 */
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_prog_text);
    glBindVertexArray(g_vao);

    glUniform2f(u_area, (float)(w * g_scale), (float)(h * g_scale));
    glUniform3f(u_color, (float)col_text.r, (float)col_text.g, (float)col_text.b);
    glUniform1f(u_dim, (float)DIM_UNSUN);
    glUniform2f(u_bounds, (float)top_dev, (float)bot_dev);

    if (g_geom.height > 1.0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, (GLint)MAX(0.0, (double)(h * g_scale) - bot_dev),
                  (GLsizei)(w * g_scale), (GLsizei)MAX(1.0, bot_dev - top_dev));
    }

    /* 按 y 排序后从上往下画：退场的行在最上面，会先画（压在最底下） */
    for (int i = 0; i < LINE_MAX; i++) {
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

    for (int i = 0; i < LINE_MAX; i++)
        g_lines[i].drawn = FALSE;

    for (int i = 0; i < n; i++) {
        Line *line = &g_lines[order[i]];

        for (int r = 0; r < line->n_rows; r++)
            draw_row(line, &line->rows[r]);
    }

    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, g_home_fbo);

    report_lines();

    return TRUE;
}

/* ---------------- 排版 ---------------- */

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
    l->char_ms = NULL;
    l->n_chars = 0;
}

/* 把整句折成若干视觉行（自动分段，不截断），
 * 同时把逐字时间轴归一化成每个字的 0..255 时间坐标。 */
static void line_rows_build(Line *l, const gchar *text)
{
    TextStyle     style;
    TextRow      *rows = NULL;
    const gint64 *char_ms = NULL;
    gsize         n_chars_total = 0;
    double        start_ms = 0.0, end_ms = 0.0;
    int           n = 0, chars = 0;
    double        h = 0.0;

    line_rows_clear(l);

    if (text == NULL || *text == '\0')
        return;

    measure_setup();

    char_ms = line_has_words(l->idx)
        ? lyrics_char_times(g_lyrics, l->idx, &n_chars_total) : NULL;

    line_span(l->idx, &start_ms, &end_ms);

    /* 有逐字时间轴时，行的结束时间至少要盖住最后一个字 */
    if (char_ms != NULL && n_chars_total > 0) {
        double last = (double)char_ms[n_chars_total - 1];

        if (last + 120.0 > end_ms)
            end_ms = last + 120.0;
    }

    style = TEXT_STYLE(FONT_MONO, g_geom.px, PANGO_WEIGHT_BOLD, g_geom.px * 0.10,
                       WHITE(1.0));

    rows = text_wrap(g_measure_cr, text, &style, g_geom.w, &n, &h);

    if (rows == NULL || n <= 0) {
        g_free(rows);
        return;
    }

    l->rows   = g_new0(Row, (gsize)n);
    l->n_rows = n;
    l->h      = h > 0.0 ? h : g_geom.px;
    l->lay_w  = g_geom.w;
    l->lay_px = g_geom.px;

    for (int i = 0; i < n; i++) {
        Row *row = &l->rows[i];

        row->text    = g_strndup(text + rows[i].start, rows[i].len);
        row->n_chars = (int)g_utf8_strlen(text + rows[i].start, (gssize)rows[i].len);
        row->dy      = rows[i].y;
        row->h       = MAX(rows[i].h, 1.0);

        if (row->n_chars <= 0) {
            g_clear_pointer(&row->text, g_free);
            chars += row->n_chars;
            continue;
        }

        /* 逐字时间 → 0..255；没有逐字时间就不建 order（整行一起亮） */
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

/* 从 index 往 direction 方向找下一个有字的行；找不到返回 -1 */
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

/* 非当前行的目标视觉参数：AMLL 的 97% 缩放 + blur(1+distance) */
static void role_for_distance(int d, double *alpha, double *scale, double *blur)
{
    if (d == 0) {                 /* 正在演出 */
        *alpha = 1.0;
        *scale = SCALE_ACTIVE;
        *blur  = 0.0;
        return;
    }

    if (d < 0) {                  /* 预推出列：刚唱完，还挂在演出位上面 */
        *alpha = PREV_ALPHA;
        *scale = SCALE_PREV;
        *blur  = BLUR_PREV;
        return;
    }

    /* 越靠下越淡越糊；第一行只轻糊，第二行拉到背景里 */
    if (d == 1) {
        *alpha = DIM_ALPHA_1;
        *scale = SCALE_INACTIVE;
        *blur  = BLUR_P1;
        return;
    }

    *alpha = CLAMP(DIM_ALPHA_1 * 0.62, 0.18, 1.0);
    *scale = SCALE_INACTIVE2;
    *blur  = MIN(BLUR_MAX, BLUR_P2);
}

/* 找这一行在动画表里的位置：先按行号（同一行要保留当前位置，让弹簧接着跑），
 * 再找空位，最后回收最不显眼的一行 */
static Line *line_for_index(gssize idx, gboolean *fresh)
{
    Line *slot = NULL;

    for (int i = 0; i < LINE_MAX; i++) {
        if (g_lines[i].text != NULL && g_lines[i].idx == idx) {
            *fresh = FALSE;
            return &g_lines[i];
        }
    }

    for (int i = 0; i < LINE_MAX && slot == NULL; i++) {
        if (g_lines[i].text == NULL)
            slot = &g_lines[i];
    }

    if (slot == NULL) {
        slot = &g_lines[0];

        for (int i = 1; i < LINE_MAX; i++) {
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

/* 把一行放进动画表：排版（需要时）+ 目标缩放 / 模糊 / 透明度 */
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
        line->exit_t    = -1.0;
        line->promote_t = -1.0;
        line->last_d    = d;
        line->alpha_tau = d == 0 ? FADE_TAU : PREVIEW_FADE_TAU;
        /* 预演行（d > 0）从下面浮上来；预推出列本来就该在那个位置 */
        line->emerge    = d > 0 ? g_geom.px * EMERGE_EM : 0.0;
        line->alpha     = d == 0 ? 0.45 : 0.0;
        line->scale     = d < 0 ? SCALE_PREV : SCALE_INACTIVE;
        line->blur      = d < 0 ? BLUR_PREV : (d == 0 ? 0.0 : BLUR_P1);
    } else if (line->exit_t >= 0.0) {
        /* 又轮回到这行了（比如往回 seek）：退场动画取消 */
        line->exit_t = -1.0;
    } else if (line->last_d > 0 && d == 0) {
        /* 从"待演出"被提拔成当前行：先缩一下再上滑放大 */
        line->promote_t = 0.0;
    }

    line->last_d = d;

    if (line->text == NULL || strcmp(line->text, text) != 0 ||
        line->lay_w != g_geom.w || line->lay_px != g_geom.px ||
        line->n_rows == 0) {
        g_free(line->text);
        line->text = g_strdup(text);
        line_rows_build(line, text);
    }

    role_for_distance(d, &alpha, &scale, &blur);

    line->alpha_t = alpha;
    line->scale_t = scale;
    line->blur_t  = blur;
    line->progress = d == 0 && line->char_ms != NULL ? scan_progress(idx) : 1.0;
    line->char_ms  = line_has_words(idx)
        ? g_lyrics->lines[idx].char_ms : NULL;
    line->n_chars  = line_has_words(idx) ? g_lyrics->lines[idx].n_chars : 0;
    line->alive      = TRUE;
    line->leave_row  = d < 0;
    line->is_current = d == 0;
    /* 越往下的行越晚起跑：上滑时两行之间的间隙会先被拉开，再收回 */
    line->y_delay   = d < 0 ? 0.0 : STAGGER_SEC * (double)(d + 1);

    return line;
}

/* 这一行从“目标变了”到真正起跑要等多久 */
static double line_delay_for(const Line *l)
{
    double d = l->y_delay;

    if (l->promote_t >= 0.0)
        d = MAX(d, PROMOTE_SHRINK);      /* 先缩一下，再上滑 */

    return d;
}

/* 放到目标位置；新行 / 大跳转直接落位，其它交给弹簧滑过去 */
static void line_place(Line *l, double y, gboolean snap)
{
    l->y_want = y;

    if (!snap && l->placed && fabs(y - l->y_pending) > 0.01) {
        /* 目标变了：这一行按自己的延迟起跑（上台那一行先等“缩一下”演完） */
        l->y_pending = y;
        l->y_wait    = line_delay_for(l);
    }

    if (snap || !l->placed) {
        /* 新露出来的预演行：从目标位置下方一点点开始，浮上来时才不突兀 */
        l->y         = y + (snap ? 0.0 : l->emerge);
        l->vy        = 0.0;
        l->y_t       = y;
        l->y_pending = y;
        l->y_wait    = 0.0;
        l->placed    = TRUE;
        l->promote_t = -1.0;   /* 直接落位，不用再演一遍上台 */
        l->emerge    = 0.0;
    }
}

void lyricgl_set_view(const Lyrics *lyrics, gssize index, const LyricViewGeom *geom)
{
    gboolean snap;

    if (g_area == NULL || geom == NULL)
        return;

    snap = (g_index < 0 || index < 0 || labs((long)(index - g_index)) > SNAP_JUMP);

    g_lyrics      = lyrics;
    g_index       = index;
    g_geom        = *geom;
    g_pos_ms      = geom->position_ms;
    g_pos_us      = now_us();
    g_pos_playing = geom->playing;
    g_anchor_no_focus = FALSE;

    for (int i = 0; i < LINE_MAX; i++)
        g_lines[i].alive = FALSE;

    /* 还没到第一行：从第一句有字的行开始铺，但整块都是"远处"的样子 */
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
        /* 没有歌词：全部淡出 */
        g_focus_idx = -1;

        for (int i = 0; i < LINE_MAX; i++) {
            if (g_lines[i].text != NULL) {
                g_lines[i].alpha_t = 0.0;
                g_lines[i].alive   = FALSE;
                g_lines[i].exit_t  = -1.0;
            }
        }

        gtk_widget_queue_draw(g_area);
        ensure_tick();
        return;
    }

    g_focus_idx = g_anchor_no_focus ? -1 : index;

    /* 一行一条日志：排查“某段时间没有歌词”时看这个最快 */
    if (index != g_logged_index) {
        g_logged_index = index;
        g_message("lyricgl: line -> %d (%s) area y=%.0f h=%.0f",
                  (int)index, lyrics->lines[index].text != NULL
                              ? lyrics->lines[index].text : "",
                  geom->top, geom->height);
    }

    /* 版式（从上到下）：预推出列 / 当前句 / 预演一 / 预演二 */
    {
        Line  *stack[ROW_MAX];
        double h[ROW_MAX];
        int    n   = 0;
        gssize idx = index;

        /* 预推出列：上一句还在场就把它挂在演出位上面（唱完还没走） */
        if (!g_anchor_no_focus) {
            gssize prev = neighbour(index, -1, 1);

            if (prev >= 0) {
                const gchar *ptext = lyrics->lines[prev].text;

                if (ptext != NULL && *ptext != '\0')
                    stack[n++] = line_begin(prev, ptext, -1);
            }
        }

        /* 当前句 */
        stack[n++] = line_begin(index, lyrics->lines[index].text,
                                g_anchor_no_focus ? 1 : 0);

        /* 往下铺两行预演 */
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

        if (n > 0) {
            double avail = geom->height - geom->px * EDGE_FADE_EM;
            double gap   = geom->gap;
            double sum = 0.0, y;

            for (int i = 0; i < n; i++) {
                h[i] = MAX(stack[i]->h, geom->px);
                sum += h[i];
            }

            /* 行数多、行又高的时候把间隙收一点，四行都露得出来；
             * 收到多少由弹簧慢慢追，所以视觉上是"间隙被拉开又收回" */
            if (n > 1) {
                double room = (avail - sum) / (double)(n - 1);

                gap = CLAMP(MIN(gap, room), geom->px * 0.20, geom->px * 0.55);
            }

            y = geom->top;

            for (int i = 0; i < n; i++) {
                line_place(stack[i], y, snap);
                y += h[i] + gap;
            }
        }
    }

    /* 不在这次排版里的行：预推出列放退场动画，其它直接淡出 */
    for (int i = 0; i < LINE_MAX; i++) {
        Line *l = &g_lines[i];

        if (l->text == NULL || l->alive)
            continue;

        if (l->leave_row && !snap && l->exit_t < 0.0) {
            l->exit_t     = 0.0;   /* 缩一下 → 滑走 */
            l->exit_slide = MAX(geom->px * 1.1, l->y_t - geom->top);
            l->leave_row  = FALSE;
            continue;
        }

        l->alpha_t = 0.0;
        l->blur_t  = 0.0;
    }

    /* 大幅跳转（seek / 换曲）：不要弹簧，直接落位 */
    if (snap) {
        for (int i = 0; i < LINE_MAX; i++) {
            Line *l = &g_lines[i];

            if (l->text == NULL)
                continue;

            l->y        = l->y_t;
            l->vy       = 0.0;
            l->scale    = l->scale_t;
            l->vscale   = 0.0;
            l->blur     = l->blur_t;
            l->alpha    = l->alpha_t;
            l->promote_t = -1.0;
        }
    }

    /* 已经淡完的回收 */
    for (int i = 0; i < LINE_MAX; i++) {
        if (g_lines[i].text != NULL && g_lines[i].alpha <= 0.003 &&
            g_lines[i].alpha_t <= 0.0 && g_lines[i].exit_t < 0.0) {
            line_rows_clear(&g_lines[i]);
            g_clear_pointer(&g_lines[i].text, g_free);
            memset(&g_lines[i], 0, sizeof(g_lines[i]));
            g_lines[i].exit_t = -1.0;
        }
    }

    gtk_widget_queue_draw(g_area);
    ensure_tick();
}

void lyricgl_clear(void)
{
    for (int i = 0; i < LINE_MAX; i++) {
        line_rows_clear(&g_lines[i]);
        g_clear_pointer(&g_lines[i].text, g_free);
        memset(&g_lines[i], 0, sizeof(g_lines[i]));
        g_lines[i].exit_t = -1.0;
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

/* ---------------- 生命周期 ---------------- */

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

    g_prog_text = 0;
    g_vao       = 0;
    g_vbo       = 0;
    g_ready     = FALSE;
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
