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

#ifndef FOLLOW_THEME_H
#define FOLLOW_THEME_H

#include <cairo.h>
#include <pango/pangocairo.h>

/* 字体：
 *   FONT_MONO   界面正文（Iosevka / Noto Sans SC）
 *   FONT_CLOCK  时钟类数字，要求等宽等高
 * 手写体（如 Segoe Print）数字宽高不一致，秒位跳变会让时钟与布局一起抖动，
 * 因此时钟类文字统一走等宽字体；测量与绘制共用同一个宏，避免两边漂移。 */
#define FONT_MONO "Iosevka,Noto Sans SC,Sarasa Mono SC,Cascadia Mono,Microsoft YaHei UI,monospace"
#define FONT_CLOCK FONT_MONO

typedef struct {
    double r, g, b, a;
} Rgba;

#define RGBA(r, g, b, a)   (Rgba){ (r), (g), (b), (a) }
#define WHITE(a)           RGBA(0.902, 0.902, 0.910, (a))

extern const Rgba col_text;    /* 主文字 */
extern const Rgba col_dim;     /* 时长 / 统计 */
extern const Rgba col_hint;    /* 模式提示 */
extern const Rgba col_faint;   /* 进度条底 */
extern const Rgba col_card;    /* 衬底,深色半透明 */
extern const Rgba col_warn;    /* 歌词 / 来源提示 */
extern const Rgba col_error;   /* 播放错误 */

typedef struct {
    const char  *family;
    double       px;
    PangoWeight  weight;
    double       letter_spacing;
    Rgba         color;
} TextStyle;

#define TEXT_STYLE(family, px, weight, spacing, color) \
    (TextStyle){ (family), (px), (weight), (spacing), (color) }

enum { TEXT_LEFT = 0, TEXT_CENTER };

double text_width(cairo_t *cr, const char *text, const TextStyle *style);
double text_height(cairo_t *cr, const char *text, const TextStyle *style);

void text_draw(cairo_t *cr, const char *text, const TextStyle *style,
               double x, double y, int align, double max_width);

double text_block_height(cairo_t *cr, const char *text, const TextStyle *style,
                         double max_width, int max_lines);

/* 墨迹（实际绘制部分）相对排版框顶部的上下边界。
 * 排版框带行距留白，需要与字形视觉对齐时应使用墨迹。 */
double text_ink_top(cairo_t *cr, const char *text, const TextStyle *style);
double text_ink_bottom(cairo_t *cr, const char *text, const TextStyle *style);

/* 把一段文字画成一张 A8 字形遮罩（给 GPU 当纹理用）：
 * 四周各留 pad 像素（发光要的余地），文字块左上角在 (pad, pad)。
 * 排版规则和 text_draw_block 完全一致（同样的宽度 / 对齐 / 行数）。
 * out_w / out_h 返回不含 pad 的文字块尺寸（像素）。失败返回 NULL。 */
cairo_surface_t *text_mask_surface(const char *text, const TextStyle *style,
                                   double max_width, int align, int max_lines,
                                   int pad, int *out_w, int *out_h);

/* 自动折行：把一段文字按 max_width 折成若干视觉行（不省略、不限行数）。
 * 每个视觉行给出它在原文里的字节区间和相对文字块顶部的纵向位置，
 * 调用方可以逐行画，长歌词就不会被截断。 */
typedef struct {
    guint  start;   /* 该行在原文里的起始字节 */
    guint  len;     /* 该行的字节数 */
    double y;       /* 行顶相对文字块顶部（逻辑像素） */
    double h;       /* 行高（逻辑像素） */
} TextRow;

/* 返回行数组（g_free 释放）；*out_n 行数、*out_h 文字块总高度。失败返回 NULL。 */
TextRow *text_wrap(cairo_t *cr, const char *text, const TextStyle *style,
                   double max_width, int *out_n, double *out_h);

/* 一次排版出两张逐像素对齐的 A8 遮罩（给歌词 GPU 层用，只处理单行文字）：
 *   out_glyph  字形本身
 *   out_order  每个字的时间坐标（0..255）
 * order[i] 是第 i 个字的时间坐标：没有逐字时间轴时传 NULL（全 0 = 整行一起亮），
 * 有逐字时间轴时由调用方按 (字时间 - 行起始) / 行时长 归一化好。
 * 尺寸（含 pad）写回 out_w / out_h。 */
gboolean text_mask_pair(const char *text, const TextStyle *style, int pad,
                        const guint8 *order, int n_order,
                        cairo_surface_t **out_glyph, cairo_surface_t **out_order,
                        int *out_w, int *out_h);

double text_draw_block(cairo_t *cr, const char *text, const TextStyle *style,
                       double x, double y, int align, double max_width, int max_lines);

/* 衬底*/
void card_draw(cairo_t *cr, double x, double y, double w, double h, double radius);

void path_round_rect(cairo_t *cr, double x, double y, double w, double h, double radius);

/* cairo_set_source_rgba*/
void color_set(cairo_t *cr, Rgba c);

double clampd(double v, double lo, double hi);

#endif /* FOLLOW_THEME_H */
