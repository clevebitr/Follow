/**
 * created: 2026/11/11
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */
#ifndef FOLLOW_UIDBG_H
#define FOLLOW_UIDBG_H

#include <gtk/gtk.h>

#include "theme.h"

/* Agent 调试通道（MCP 服务 gtk-mcp-agent-capi）。
 *
 *   FOLLOW_UIDBG=1                打开；默认端口 7878
 *   FOLLOW_UIDBG_PORT=7878        端口
 */

enum {
    UIDBG_LAYER_WALL = 0,     /* 歌曲墙（cairo 回退时的磁贴） */
    UIDBG_LAYER_WALL_CHROME,  /* 墙的表头 / 底栏 / 帮助页 */
    UIDBG_LAYER_STAGE,        /* 主画面 */
    UIDBG_LAYER_LYRIC,        /* 歌词（GPU 层，单独标出来） */
    UIDBG_LAYER_PALETTE,      /* 命令面板（cairo 部分） */
    UIDBG_LAYER_PERF,         /* 性能检测层（perf.c 的读数） */
    UIDBG_LAYER_MAX
};

gboolean uidbg_enabled(void);
void     uidbg_start(GtkWidget *window);
void     uidbg_stop(void);

/* 绘制侧埋点 */
/* 当前这一批记录算哪一层（渲染函数开头） */
void uidbg_begin_layer(int layer);

/* 清掉某一层的记录（该层准备重画时调） */
void uidbg_layer_clear(int layer);

/* 录一段文字 / 一块衬底。关掉调试时是空调用。 */
void uidbg_record_text(double x, double y, double w, double h,
                       double px, Rgba color, const gchar *text);
void uidbg_record_rect(double x, double y, double w, double h,
                       Rgba color, const gchar *tag);

/* 一帧的开始 / 结束（stage_render 里调用，用来算 fps 和清上一帧记录） */
void uidbg_frame_begin(void);
void uidbg_frame_end(void);

/* 截图回退路径会把同一个画面再画一遍：这期间别重复记录 */
void uidbg_suppress(gboolean on);

/* 由 main.c 实现的输入注入 */

gboolean uidbg_hook_key(guint keyval, GdkModifierType state);
void     uidbg_hook_click(double x, double y, gboolean double_click);
void     uidbg_hook_scroll(double x, double y, double dx, double dy);
void     uidbg_hook_motion(double x, double y);
void     uidbg_hook_text(const gchar *text);

/* 绝对跳转（秒）：调试歌词分句 / 空行 / 间奏行为时直接落到指定位置 */
void     uidbg_hook_seek(double seconds);

#endif /* FOLLOW_UIDBG_H */
