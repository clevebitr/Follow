/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_PLAYLIST_H
#define FOLLOW_PLAYLIST_H

#include <cairo.h>
#include <glib.h>

/* 无限歌曲墙：block 二维重复，方向键空间选择，Enter 播放。 */

void     playlist_toggle(void);
void     playlist_open_page(void);
void     playlist_close(void);
gboolean playlist_is_open(void);
gboolean playlist_is_closing(void);
gboolean playlist_animating(void);
gboolean playlist_chrome_animating(void);
gboolean playlist_frame_pending(void);
void     playlist_request_frame(void);

void     playlist_move(gint dx, gint dy);
void     playlist_page(gint dir);
void     playlist_home(void);
void     playlist_end(void);
gboolean playlist_activate(void);

void     playlist_draw(cairo_t *cr, double w, double h);
void     playlist_build_frame(double w, double h);
void     playlist_draw_chrome(cairo_t *cr, double w, double h);

/* GL 纹理缓存；surface 由 playlist 持有，调用方不得释放。 */
cairo_surface_t *playlist_tile_surface_cached(gsize track, int w, int h, int scale);
void             playlist_tile_request(gsize track, int w, int h, int scale);
cairo_surface_t *playlist_tile_overlay(gsize track, int w, int h,
                                       gboolean selected, gboolean hovered,
                                       gboolean current, int scale);

void     playlist_pan_begin(double x, double y);
void     playlist_pan_update(double offset_x, double offset_y);
void     playlist_pan_end(double offset_x, double offset_y);
void     playlist_pan_by(double dx, double dy);
gboolean playlist_was_dragging(void);
gboolean playlist_click(double x, double y);
void     playlist_hover(double x, double y);

void     playlist_clear(void);
void     playlist_reload(void);

#endif
