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

/*字体：
 *   FONT_MONO  Iosevka / Noto Sans SC
 *   FONT_HAND
 * */


#define FONT_MONO "Iosevka,Noto Sans SC,Sarasa Mono SC,Cascadia Mono,Microsoft YaHei UI,monospace"
#define FONT_HAND "Segoe Print,Caveat,Noto Sans SC,Microsoft YaHei UI,cursive"

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
double text_draw_block(cairo_t *cr, const char *text, const TextStyle *style,
                       double x, double y, int align, double max_width, int max_lines);

/* 衬底*/
void card_draw(cairo_t *cr, double x, double y, double w, double h, double radius);

void path_round_rect(cairo_t *cr, double x, double y, double w, double h, double radius);

/* cairo_set_source_rgba*/
void color_set(cairo_t *cr, Rgba c);

double clampd(double v, double lo, double hi);

#endif /* FOLLOW_THEME_H */
