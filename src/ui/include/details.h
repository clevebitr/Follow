/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_DETAILS_H
#define FOLLOW_DETAILS_H

#include <cairo.h>
#include <gdk/gdk.h>
#include <glib.h>

/* 歌曲详情页（快捷键默认 i）。
 *
 * 数据来自 metadata 状态机：
 *   READY   → 曲名 / 艺人 / 专辑 / MBID / 发行时间 / 厂牌 / 曲序 / 标签
 *   PICK    → 首次识别：候选列表，↑↓ 选择、Enter 确认
 *   LOADING → 查询中
 *   ERROR   → 失败原因 + Enter/R 重试
 *
 * 页面整屏绘制，风格与主画面一致（cairo + 同一套字体 / 卡片 / 种子色）。 */

void     details_open(void);
void     details_close(void);
void     details_toggle(void);
gboolean details_is_open(void);

/* 详情页打开时的按键分发：TRUE = 已消费（页面自己吞掉所有键）。 */
gboolean details_key(guint keyval, GdkModifierType state);

/* 整屏绘制。 */
void details_draw(cairo_t *cr, double width, double height);

#endif /* FOLLOW_DETAILS_H */
