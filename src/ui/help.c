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

#include "help.h"

#include "keymap.h"
#include "theme.h"

#define ROWS_PER_PANEL 2.2      /* 标题 + 底部留白占的行数 */

void help_draw(cairo_t *cr, double width, double height)
{
    static const struct {
        KeyAction   action;
        const char *desc;
    } rows[] = {
        { KEY_PLAY_PAUSE,   "播放 / 暂停（还没放歌时从第一首开始）" },
        { KEY_SEEK_BACK,    "后退 5 秒（按住 Shift = 30 秒）" },
        { KEY_SEEK_FORWARD, "前进 5 秒（按住 Shift = 30 秒）" },
        { KEY_NEXT,         "下一首" },
        { KEY_PREV,         "上一首" },
        { KEY_QUEUE_PREV,   "NEXT 预告：往前一首" },
        { KEY_QUEUE_NEXT,   "NEXT 预告：往后一首" },
        { KEY_RESTART,      "从头重放当前曲目" },
        { KEY_STOP,         "停止" },
        { KEY_REPEAT,       "循环：关 → 列表 → 单曲" },
        { KEY_SHUFFLE,      "随机播放开关" },
        { KEY_LYRIC_EARLIER, "歌词整体提前 250 ms（视频带片头时用）" },
        { KEY_LYRIC_LATER,   "歌词整体延后 250 ms" },
        { KEY_LYRIC_RESET,   "歌词偏移恢复成 ini 里的值" },
        { KEY_PLAYLIST,     "打开 / 关闭播放列表" },
        { KEY_FULLSCREEN,   "全屏切换" },
        { KEY_HELP,         "打开 / 关闭这一页" },
        { KEY_PERF,         "性能检测层（帧率 / 各层耗时）" },
        { KEY_BACK,         "关闭这一页 / 退出全屏" },
        { KEY_QUIT,         "退出" },
    };
    const double n_rows  = (double)G_N_ELEMENTS(rows) + ROWS_PER_PANEL;
    double       panel_w = clampd(width * 0.56, 420.0, 900.0);
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
    key_x   = panel_x + panel_w * 0.07;
    desc_x  = panel_x + panel_w * 0.34;

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

        text_draw(cr, keymap_label(rows[i].action), &key_style, key_x, y,
                  TEXT_LEFT, desc_x - key_x - 12.0);
        text_draw(cr, rows[i].desc, &desc_style, desc_x, y, TEXT_LEFT,
                  panel_x + panel_w * 0.94 - desc_x);
    }
}
