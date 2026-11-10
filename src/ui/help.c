/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
*/

#include "help.h"

#include "theme.h"

#define ROWS_PER_PANEL 2.2      /* 标题 + 底部留白占的行数 */

void help_draw(cairo_t *cr, double width, double height)
{
    static const struct {
        const char *key;
        const char *desc;
    } rows[] = {
        { "SPACE",   "播放 / 暂停" },
        { "←  →",    "后退 / 前进 5 秒（按住 Shift = 30 秒）" },
        { "N / P",   "下一首 / 上一首" },
        { "R",       "从头重放当前曲目" },
        { "S",       "停止" },
        { "L",       "循环：关 → 列表 → 单曲" },
        { "H",       "随机播放开关" },
        { "[ / ]",   "歌词整体提前 / 延后 250 ms（视频带片头时用）" },
        { "\\",      "歌词偏移恢复成 ini 里的值" },
        { "F / F11", "全屏切换" },
        { "F1",      "打开 / 关闭这一页" },
        { "Q",       "退出" },
        { "ESC",     "关闭这一页 / 退出全屏" },
    };
    const double n_rows  = (double)G_N_ELEMENTS(rows) + ROWS_PER_PANEL;
    double       panel_w = clampd(width * 0.50, 360.0, 760.0);
    double       row_h   = clampd(height * 0.047, 16.0, 34.0);
    double       panel_h = row_h * n_rows;
    double       panel_x, panel_y, key_x, desc_x;
    TextStyle    title_style, key_style, desc_style;

    if (panel_h > height * 0.94) {
        row_h   = height * 0.94 / n_rows;
        panel_h = row_h * n_rows;
    }

    panel_x = (width - panel_w) * 0.5;
    panel_y = (height - panel_h) * 0.5;
    key_x   = panel_x + panel_w * 0.10;
    desc_x  = panel_x + panel_w * 0.30;

    /* 压暗整屏 */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.62);
    cairo_rectangle(cr, 0.0, 0.0, width, height);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.071, 0.071, 0.078, 0.97);
    path_round_rect(cr, panel_x, panel_y, panel_w, panel_h, 10.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.902, 0.902, 0.910, 0.14);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    title_style = TEXT_STYLE(FONT_MONO, row_h * 0.62, PANGO_WEIGHT_NORMAL,
                             row_h * 0.16, col_dim);
    key_style   = TEXT_STYLE(FONT_MONO, row_h * 0.52, PANGO_WEIGHT_BOLD,
                             row_h * 0.10, col_text);
    desc_style  = TEXT_STYLE(FONT_MONO, row_h * 0.52, PANGO_WEIGHT_NORMAL,
                             0.0, col_dim);

    text_draw(cr, "SHORTCUTS", &title_style, key_x, panel_y + row_h * 0.75,
              TEXT_LEFT, panel_w - (key_x - panel_x) * 2.0);

    for (gsize i = 0; i < G_N_ELEMENTS(rows); i++) {
        double y = panel_y + row_h * (1.9 + (double)i);

        text_draw(cr, rows[i].key, &key_style, key_x, y, TEXT_LEFT, 0.0);
        text_draw(cr, rows[i].desc, &desc_style, desc_x, y, TEXT_LEFT,
                  panel_x + panel_w * 0.94 - desc_x);
    }
}
