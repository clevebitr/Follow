#ifndef FOLLOW_THEME_H
#define FOLLOW_THEME_H

#include <cairo.h>
#include <pango/pangocairo.h>

/* 字体、颜色、以及全部文字绘制原语。
 *
 * 字体：
 *   FONT_MONO  界面文字（备注 / 歌词 / 曲名 / 胶囊 / 帮助页 / 统计）
 *              拉丁和数字用 Iosevka，中文用 Noto Sans SC —— 两个都是开源字体，
 *              后面再挂系统等宽字体兜底（都没装也不会变成方块）
 *   FONT_HAND  只有"时间"用手写体（大时钟、左下角墙钟）—— Sowon2 的招牌
 *
 * 其余模块只说"用什么样式、画在哪"，不再各写一遍 Pango 样板。 */

/* 注意：Pango 的家族列表按逗号切分且**不去空格** —— "Iosevka, Noto Sans SC"
 * 里的第二个家族会变成 " Noto Sans SC"（带前导空格）从而找不到，中文就会变豆腐块。
 * 所以逗号后面一定不能有空格。 */
#define FONT_MONO "Iosevka,Noto Sans SC,Sarasa Mono SC,Cascadia Mono,Microsoft YaHei UI,monospace"
#define FONT_HAND "Segoe Print,Caveat,Noto Sans SC,Microsoft YaHei UI,cursive"

/* ---- 颜色：一支白的不同透明度，配近黑底 ---- */
typedef struct {
    double r, g, b, a;
} Rgba;

#define RGBA(r, g, b, a)   (Rgba){ (r), (g), (b), (a) }
#define WHITE(a)           RGBA(0.902, 0.902, 0.910, (a))

extern const Rgba col_text;    /* 主文字 */
extern const Rgba col_dim;     /* 次级：时长 / 统计 */
extern const Rgba col_hint;    /* 第三级：模式提示 */
extern const Rgba col_faint;   /* 只剩轮廓：进度条底 */
extern const Rgba col_card;    /* 衬底：深色半透明，让文字 / 封面离开花背景 */
extern const Rgba col_warn;    /* 歌词 / 来源提示 */
extern const Rgba col_error;   /* 播放错误 */

/* ---- 文字样式 ---- */
typedef struct {
    const char  *family;
    double       px;             /* 字号（像素） */
    PangoWeight  weight;
    double       letter_spacing; /* 字距（像素），0 = 不设 */
    Rgba         color;
} TextStyle;

/* 便捷构造：一行写清 family / 字号 / 字重 / 字距 / 颜色 */
#define TEXT_STYLE(family, px, weight, spacing, color) \
    (TextStyle){ (family), (px), (weight), (spacing), (color) }

enum { TEXT_LEFT = 0, TEXT_CENTER };

/* 单行文本的像素尺寸（空串 / NULL 返回 0） */
double text_width(cairo_t *cr, const char *text, const TextStyle *style);
double text_height(cairo_t *cr, const char *text, const TextStyle *style);

/* 画一行文字。y 是文本框顶部；
 *   align == TEXT_CENTER → x 是中心线；
 *   max_width > 0       → 限宽 + 结尾省略号（居中时按 max_width 居中）。
 * 空串直接返回。 */
void text_draw(cairo_t *cr, const char *text, const TextStyle *style,
               double x, double y, int align, double max_width);

/* 多行文本：按 max_width 自动换行（中文按字断，英文按词断），
 * max_lines > 0 时超过就省略。返回实际高度。 */
double text_block_height(cairo_t *cr, const char *text, const TextStyle *style,
                         double max_width, int max_lines);
double text_draw_block(cairo_t *cr, const char *text, const TextStyle *style,
                       double x, double y, int align, double max_width, int max_lines);

/* 深色圆角衬底（封面 / 歌词 / 胶囊 / 统计共用） */
void card_draw(cairo_t *cr, double x, double y, double w, double h, double radius);

/* 圆角矩形路径（只建路径，填充/描边由调用方决定） */
void path_round_rect(cairo_t *cr, double x, double y, double w, double h, double radius);

/* cairo_set_source_rgba 的简写 */
void color_set(cairo_t *cr, Rgba c);

double clampd(double v, double lo, double hi);

#endif /* FOLLOW_THEME_H */
