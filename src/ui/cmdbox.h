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

#ifndef FOLLOW_CMDBOX_H
#define FOLLOW_CMDBOX_H

#include <gtk/gtk.h>

/* 右下角命令面板：
 *   - 输入框在底部，建议列表向上展开
 *   - Ctrl+Shift+P 开关，↑/↓ 选择，Enter 执行，Esc 关闭
 *   - 命令：重载 ini、音量、输出设备、可视化、动效、循环/随机、歌词、墙、帮助、全屏、退出 */

GtkWidget *palette_new(void);
void       palette_open(void);
void       palette_open_query(const gchar *query);
void       palette_close(void);
void       palette_toggle(void);
gboolean   palette_is_open(void);
gboolean   palette_contains(double x, double y);

/* 仅 FOLLOW_PALETTE_AUTO 测试用：模拟 enter/down/up/esc/text:foo 序列。 */
void       palette_debug_steps(const gchar *steps);

#endif /* FOLLOW_CMDBOX_H */
