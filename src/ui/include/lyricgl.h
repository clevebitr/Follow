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

#ifndef FOLLOW_LYRICGL_H
#define FOLLOW_LYRICGL_H

#include <gtk/gtk.h>

#include "lyrics.h"
#include "theme.h"

/* 歌词视图：GPU 渲染。
 *
 * CPU 只负责排版（Pango 自动折行 → 每个视觉行一张 A8 字形遮罩 + 一张逐字
 * 时间坐标遮罩），采样、扫描头、渐隐在 GPU 上。
 *
 * 排版规则：
 *   - 左对齐，从区域左上角开始往下排
 *   - 当前行贴区域顶，往下依次是待唱的下一行（越远越淡）
 *   - 一句太长就自动折成多行，逐行绘制，不做"最多几行 + 省略号"的截断
 *   - 没有辉光、没有模糊（不发光也不做玻璃效果）
 *
 * 扫描头：当前行唱到哪，字就亮到哪（没唱到的压暗），折行后跨行连续推进。 */

typedef struct {
    double x, w;        /* 文字块左边界 / 折行宽度（逻辑像素） */
    double top;         /* 歌词可视区域上沿（逻辑像素） */
    double height;      /* 歌词可视区域高度（逻辑像素） */
    double px;          /* 字号（逻辑像素） */
    double gap;         /* 行间距（逻辑像素） */
    double position_ms; /* 播放位置（已减掉歌词偏移），用来算当前行唱到哪了 */
    gboolean playing;   /* 暂停时扫描头不动 */
    Rgba   accent;      /* 当前句高亮色（封面种子色），a <= 0 时退回普通白 */
} LyricViewGeom;

GtkWidget *lyricgl_new(void);

/* GL 上下文 + shader 就绪 */
gboolean   lyricgl_ready(void);

/* 一次排版：把当前行和后面的行放进动画表。 */
void       lyricgl_set_view(const Lyrics *lyrics, gssize index,
                            const LyricViewGeom *geom);

/* 停掉一切并清空（换曲目 / 退出） */
void       lyricgl_clear(void);

/* 窗口拖动 / 缩放期间暂停逐帧重绘（此时整窗每帧重合成，叠加歌词动画会掉帧）。
 * 传入 FALSE 时恢复并补绘一帧。 */
void       lyricgl_drag_state(gboolean dragging);

#endif /* FOLLOW_LYRICGL_H */
