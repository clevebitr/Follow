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

#ifndef FOLLOW_PLAYLIST_H
#define FOLLOW_PLAYLIST_H

#include <cairo.h>
#include <glib.h>

/* 无限歌曲墙：12x8 block 在二维方向重复，每次进入随机选择模板 / 翻转 / 顺序。
 * 鼠标拖动或滚轮自由平移，方向键在磁贴之间空间移动，Enter 播放并返回。
 * 动效：页面滑入/滑出、选中磁贴弹出、封面淡入、背景主色过渡。 */

void     playlist_toggle(void);
void     playlist_open_page(void);
void     playlist_close(void);
gboolean playlist_is_open(void);
gboolean playlist_is_closing(void);
gboolean playlist_animating(void);
gboolean playlist_chrome_animating(void);   /* 进场 / 退场时 chrome 层也要重画 */
gboolean playlist_frame_pending(void);       /* GL 墙是否有新的帧要提交 */
void     playlist_request_frame(void);       /* 标记一帧脏（鼠标 / 焦点变化） */

/* 方向键：dx/dy 取 -1 / 0 / 1 */
void     playlist_move(gint dx, gint dy);
void     playlist_page(gint dir);
void     playlist_home(void);
void     playlist_end(void);
gboolean playlist_activate(void);

void     playlist_draw(cairo_t *cr, double w, double h);                 /* cairo 回退：整页 */
void     playlist_build_frame(double w, double h);                       /* GL 层：推一帧磁贴 */
void     playlist_draw_chrome(cairo_t *cr, double w, double h);          /* GL 层：表头 / 底栏 / 空列表 */

/* 给 GL 纹理用的 cairo 缓存；surface 由 playlist 持有，调用方不要 destroy。
 * overlay 四周比磁贴多 4px（选中描边的外圈）。 */
cairo_surface_t *playlist_tile_surface_cached(gsize track, int w, int h, int scale);
void             playlist_tile_request(gsize track, int w, int h, int scale);
cairo_surface_t *playlist_tile_overlay(gsize track, int w, int h,
                                       gboolean selected, gboolean hovered,
                                       gboolean current, int scale);

/* 鼠标：拖动 / 滚轮 / 悬停 / 点击 */
void     playlist_pan_begin(double x, double y);
void     playlist_pan_update(double offset_x, double offset_y);
void     playlist_pan_end(double offset_x, double offset_y);
void     playlist_pan_by(double dx, double dy);
gboolean playlist_was_dragging(void);
gboolean playlist_click(double x, double y);
void     playlist_hover(double x, double y);

void     playlist_clear(void);

#endif /* FOLLOW_PLAYLIST_H */
