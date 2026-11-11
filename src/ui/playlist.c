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

#include "playlist.h"

#include <math.h>
#include <string.h>

#include "config.h"
#include "covercache.h"
#include "fx.h"
#include "keymap.h"
#include "palette.h"
#include "platform.h"
#include "player.h"
#include "theme.h"
#include "ui.h"
#include "wallgl.h"

/* 无限歌曲墙。12x8 的 block 在二维方向重复；每个 block 从几个模板里随机选
 * 一个、随机翻转、随机打乱歌曲顺序。布局思路参考 Folia 的 poster wall，
 * 槽位和实现是我们自己的。 */

#define PL_MAX_HITS 320
#define PL_TEMPLATE_MAX 12
#define PL_TILE_CACHE_MAX 96

typedef struct {
    int x, y, cols, rows;
} PlSlot;

typedef struct {
    int    count;
    PlSlot slots[PL_TEMPLATE_MAX];
} PlTemplate;

typedef struct {
    int    track;
    int    bx, by, slot;
    double x, y, w, h;      /* 屏幕坐标 */
} PlHit;

static void tile_cache_clear(void);

static struct {
    gboolean open;
    gboolean closing;
    guint    seed;

    Fx       enter_fx;
    Fx       close_fx;
    gint64   last_draw_us;

    /* 相机：cam 是世界坐标里视口左上角的位置 */
    double   cam_x, cam_y;
    double   cam_tx, cam_ty;
    double   pan_vx, pan_vy;
    gboolean dragging;
    gboolean drag_moved;
    double   drag_start_x, drag_start_y;
    double   drag_cam_x, drag_cam_y;
    double   drag_last_x, drag_last_y;
    gint64   drag_last_us;
    gint64   last_drag_end_us;

    /* 选中的磁贴实例 */
    gboolean focus_valid;
    int      focus_bx, focus_by, focus_slot, focus_track;
    double   focus_wx, focus_wy, focus_ww, focus_wh;
    gint64   sel_us;

    /* 鼠标悬停的实例 */
    gboolean hover_valid;
    int      hover_bx, hover_by, hover_slot;

    /* 页面几何 */
    double   cell, gap, pitch, block_w, block_h;
    double   pad_x, header_h, bottom_h;
    double   screen_w, screen_h;

    /* 背景：跟随当前 focus 封面的主色缓慢过渡 */
    Palette  bg_pal;
    gboolean bg_has;
    gboolean bg_moving;

    /* 最近一帧的进场 / 退场进度；GL 与 chrome 层共用 */
    double   vis;
    gboolean gl_chrome;      /* 当前帧墙由 GL 层绘制，chrome 层负责表头 / 底栏 */
    gboolean frame_dirty;    /* GL 墙需要重建一帧 */
    int      last_track;     /* 当前曲目变化时刷新“正在播放”标记 */
    int      last_chrome_track; /* 表头 / 底栏当前展示的 focus 曲目 */

    PlHit    hit[PL_MAX_HITS];
    int      n_hits;

    gchar   *message;
} P;

/* 构建 GL 帧时复用，避免每帧 malloc/free */
static WallGlTile g_frame_tiles[PL_MAX_HITS + 1];

/* 三个 12x8 的 exact-cover 模板。尺寸混合不同，随机选一个就不会每次长得一样。 */
static const PlTemplate g_templates[] = {
    {
        12,
        {
            { 3, 0, 6, 4 }, { 0, 0, 3, 2 }, { 0, 2, 3, 2 }, { 9, 0, 3, 2 },
            { 0, 4, 4, 4 }, { 9, 2, 3, 2 }, { 9, 4, 3, 2 }, { 9, 6, 3, 2 },
            { 4, 4, 3, 2 }, { 7, 4, 2, 2 }, { 4, 6, 3, 2 }, { 7, 6, 2, 2 },
        }
    },
    {
        10,
        {
            { 0, 0, 6, 4 }, { 6, 0, 6, 4 },
            { 0, 4, 3, 2 }, { 3, 4, 3, 2 }, { 6, 4, 3, 2 }, { 9, 4, 3, 2 },
            { 0, 6, 3, 2 }, { 3, 6, 3, 2 }, { 6, 6, 3, 2 }, { 9, 6, 3, 2 },
        }
    },
    {
        12,
        {
            { 0, 0, 4, 4 }, { 4, 0, 4, 4 }, { 8, 0, 2, 2 }, { 10, 0, 2, 2 },
            { 8, 2, 2, 2 }, { 10, 2, 2, 2 },
            { 0, 4, 4, 4 }, { 4, 4, 4, 4 }, { 8, 4, 2, 2 }, { 10, 4, 2, 2 },
            { 8, 6, 2, 2 }, { 10, 6, 2, 2 },
        }
    },
};

static gint64 now_us(void)
{
    return g_get_monotonic_time();
}

static gchar *format_time(double seconds)
{
    int total = (int)(MAX(seconds, 0.0) + 0.5);

    if (total >= 3600)
        return g_strdup_printf("%d:%02d:%02d", total / 3600,
                               (total / 60) % 60, total % 60);

    return g_strdup_printf("%02d:%02d", total / 60, total % 60);
}

static Platform track_platform(const AudioTrack *track)
{
    Platform p;

    if (track == NULL)
        return PLATFORM_UNKNOWN;

    if (track->platform != NULL && *track->platform != '\0' &&
        (p = platform_from_name(track->platform)) != PLATFORM_UNKNOWN)
        return p;

    return platform_from_url(track->page != NULL ? track->page : track->link);
}

static void hash_color(const AudioTrack *track, double *r, double *g, double *b)
{
    static const double base[8][3] = {
        { 0.34, 0.17, 0.24 }, { 0.16, 0.25, 0.38 }, { 0.17, 0.32, 0.26 },
        { 0.38, 0.27, 0.15 }, { 0.27, 0.18, 0.36 }, { 0.15, 0.32, 0.36 },
        { 0.36, 0.22, 0.16 }, { 0.22, 0.28, 0.20 },
    };
    guint h = g_str_hash(track != NULL && track->title != NULL ? track->title
                        : (track != NULL && track->link != NULL ? track->link : ""));
    const double *c = base[h % G_N_ELEMENTS(base)];

    *r = c[0];
    *g = c[1];
    *b = c[2];
}

/* ---------------- 无限 block 几何 ---------------- */

static guint32 mix32(guint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static guint32 block_hash(int bx, int by)
{
    guint32 h = P.seed ^ 0x9e3779b9u;

    h ^= mix32((guint32)bx * 0x9e3779b1u);
    h ^= mix32((guint32)by * 0x85ebca6bu);

    return mix32(h);
}

static const PlTemplate *block_template(guint32 h)
{
    return &g_templates[h % G_N_ELEMENTS(g_templates)];
}

static int block_flip(guint32 h)
{
    return (int)((h >> 8) & 3u);
}

/* 每个 block 都重新洗一遍 track 顺序，重复到填满槽位为止 */
static void block_order(guint32 h, int n, int count, int *order)
{
    guint32 state = mix32(h ^ 0xa511e9b3u);
    int     i;

    if (n <= 0 || count <= 0)
        return;

    for (i = 0; i < count; i++)
        order[i] = i % n;

    for (i = count - 1; i > 0; i--) {
        state = state * 1664525u + 1013904223u;

        int j = (int)((state >> 8) % (guint32)(i + 1));
        int t = order[i];

        order[i] = order[j];
        order[j] = t;
    }
}

static void update_metrics(double w, double h)
{
    if (w != P.screen_w || h != P.screen_h)
        P.frame_dirty = TRUE;

    P.screen_w  = w;
    P.screen_h  = h;
    P.pad_x     = clampd(w * 0.035, 18.0, 64.0);
    P.gap       = clampd(h * 0.010, 7.0, 14.0);
    P.cell      = clampd(h * 0.078, 50.0, 124.0);
    P.pitch     = P.cell + P.gap;
    /* 和 Folia 的 layout.ts 一致：block 本身也占一个完整 pitch，
     * 块与块之间才有一致的间距（12*cell + 11*gap 会在接缝处丢掉这个 gap）。 */
    P.block_w   = 12.0 * P.pitch;
    P.block_h   = 8.0 * P.pitch;
    P.header_h  = clampd(h * 0.145, 72.0, 164.0);
    P.bottom_h  = clampd(h * 0.105, 54.0, 104.0);
}

static void slot_rect(int bx, int by, const PlTemplate *tpl, int flip, int slot,
                      double *x, double *y, double *w, double *h)
{
    PlSlot s = tpl->slots[slot];
    int    sx = (flip & 1) ? 12 - s.x - s.cols : s.x;
    int    sy = (flip & 2) ? 8 - s.y - s.rows : s.y;

    *x = (double)bx * P.block_w + (double)sx * P.pitch;
    *y = (double)by * P.block_h + (double)sy * P.pitch;
    *w = (double)s.cols * P.pitch - P.gap;
    *h = (double)s.rows * P.pitch - P.gap;
}

static void focus_refresh_world(void)
{
    const PlTemplate *tpl;
    guint32           h;

    if (!P.focus_valid)
        return;

    h   = block_hash(P.focus_bx, P.focus_by);
    tpl = block_template(h);
    slot_rect(P.focus_bx, P.focus_by, tpl, block_flip(h), P.focus_slot,
              &P.focus_wx, &P.focus_wy, &P.focus_ww, &P.focus_wh);
}

static void view_size(double *w, double *h)
{
    int cw = app.canvas != NULL ? gtk_widget_get_width(app.canvas) : 0;
    int ch = app.canvas != NULL ? gtk_widget_get_height(app.canvas) : 0;

    *w = cw > 0 ? (double)cw : P.screen_w;
    *h = ch > 0 ? (double)ch : P.screen_h;
}

static void focus_center_camera(gboolean instant)
{
    double w, h;

    if (!P.focus_valid)
        return;

    view_size(&w, &h);

    P.cam_tx = P.focus_wx + P.focus_ww * 0.5 - w * 0.5;
    P.cam_ty = P.focus_wy + P.focus_wh * 0.5 - h * 0.5;

    if (instant || !config_animations()) {
        P.cam_x = P.cam_tx;
        P.cam_y = P.cam_ty;
        P.pan_vx = P.pan_vy = 0.0;
    }

    ui_animate();
    ui_request_redraw();
}

static void focus_set_instance(int bx, int by, int slot, int track, gboolean center)
{
    guint32           h = block_hash(bx, by);
    const PlTemplate *tpl = block_template(h);

    if (slot < 0 || slot >= tpl->count || track < 0)
        return;

    P.focus_valid = TRUE;
    P.focus_bx    = bx;
    P.focus_by    = by;
    P.focus_slot  = slot;
    P.focus_track = track;
    P.sel_us      = now_us();
    P.frame_dirty = TRUE;

    focus_refresh_world();

    if (center)
        focus_center_camera(FALSE);

    ui_request_redraw();
}

static void focus_find_track(int track, int center_bx, int center_by, int radius)
{
    const APPconfig *cfg = config_get();
    int              n   = (int)cfg->n_tracks;
    int              best_dist = G_MAXINT;
    int              best_bx = 0, best_by = 0, best_slot = 0, best_track = 0;
    gboolean         found = FALSE;

    if (n <= 0)
        return;

    for (int r = 0; r <= radius; r++) {
        for (int oy = -r; oy <= r; oy++) {
            for (int ox = -r; ox <= r; ox++) {
                int bx, by, dist;
                guint32 h;
                const PlTemplate *tpl;
                int order[PL_TEMPLATE_MAX];

                if (r > 0 && MAX(ABS(ox), ABS(oy)) != r)
                    continue;

                bx = center_bx + ox;
                by = center_by + oy;
                dist = ox * ox + oy * oy;

                if (dist >= best_dist)
                    continue;

                h    = block_hash(bx, by);
                tpl  = block_template(h);
                block_order(h, n, tpl->count, order);

                for (int s = 0; s < tpl->count; s++) {
                    if (order[s] != track)
                        continue;

                    best_dist  = dist;
                    best_bx    = bx;
                    best_by    = by;
                    best_slot  = s;
                    best_track = track;
                    found      = TRUE;
                    break;
                }
            }
        }
    }

    if (!found) {
        guint32 h = block_hash(0, 0);
        const PlTemplate *tpl = block_template(h);
        int order[PL_TEMPLATE_MAX];

        block_order(h, n, tpl->count, order);
        best_bx = 0;
        best_by = 0;
        best_slot = 0;
        best_track = order[0];
    }

    focus_set_instance(best_bx, best_by, best_slot, best_track, TRUE);
}

static void focus_nearest_visible(void)
{
    const APPconfig *cfg = config_get();
    int              n   = (int)cfg->n_tracks;
    double           w, h, cx, cy;
    double           best_dist = 1e30;
    int              best_bx = 0, best_by = 0, best_slot = 0, best_track = 0;
    gboolean         found = FALSE;
    double           overscan;

    if (n <= 0)
        return;

    view_size(&w, &h);
    cx = P.cam_x + w * 0.5;
    cy = P.cam_y + h * 0.5;
    overscan = P.cell * 2.0;

    for (int by = (int)floor((P.cam_y - overscan) / P.block_h);
         by <= (int)floor((P.cam_y + h + overscan) / P.block_h); by++) {
        for (int bx = (int)floor((P.cam_x - overscan) / P.block_w);
             bx <= (int)floor((P.cam_x + w + overscan) / P.block_w); bx++) {
            guint32 h = block_hash(bx, by);
            const PlTemplate *tpl = block_template(h);
            int order[PL_TEMPLATE_MAX];

            block_order(h, n, tpl->count, order);

            for (int s = 0; s < tpl->count; s++) {
                double x, y, tw, th, dx, dy, dist;

                slot_rect(bx, by, tpl, block_flip(h), s, &x, &y, &tw, &th);
                dx = x + tw * 0.5 - cx;
                dy = y + th * 0.5 - cy;
                dist = dx * dx + dy * dy;

                if (dist < best_dist) {
                    best_dist = dist;
                    best_bx = bx;
                    best_by = by;
                    best_slot = s;
                    best_track = order[s];
                    found = TRUE;
                }
            }
        }
    }

    if (found) {
        P.focus_valid = TRUE;
        P.focus_bx    = best_bx;
        P.focus_by    = best_by;
        P.focus_slot  = best_slot;
        P.focus_track = best_track;
        P.sel_us      = now_us();
        focus_refresh_world();
    }
}

/* 用户自由拖动后 focus 可能在屏幕外：下一次方向键先落到视野内最近的磁贴 */
static void focus_sync_to_view(void)
{
    double w, h, sx, sy;

    if (!P.focus_valid) {
        focus_nearest_visible();
        return;
    }

    view_size(&w, &h);
    sx = P.focus_wx + P.focus_ww * 0.5 - P.cam_x;
    sy = P.focus_wy + P.focus_wh * 0.5 - P.cam_y;

    if (sx < w * 0.12 || sx > w * 0.88 ||
        sy < P.header_h * 1.05 || sy > h - P.bottom_h * 1.05)
        focus_nearest_visible();
}

static void focus_move(gint dx, gint dy)
{
    const APPconfig *cfg = config_get();
    int              n   = (int)cfg->n_tracks;
    double           fcx, fcy;
    double           best_score = 1e30;
    int              best_bx = 0, best_by = 0, best_slot = 0, best_track = 0;
    gboolean         found = FALSE;

    if (!P.open || P.closing || n <= 0 || (dx == 0 && dy == 0))
        return;

    P.pan_vx = P.pan_vy = 0.0;
    focus_sync_to_view();

    if (!P.focus_valid)
        return;

    fcx = P.focus_wx + P.focus_ww * 0.5;
    fcy = P.focus_wy + P.focus_wh * 0.5;

    for (int oy = -1; oy <= 1; oy++) {
        for (int ox = -1; ox <= 1; ox++) {
            int bx = P.focus_bx + ox;
            int by = P.focus_by + oy;
            guint32 h = block_hash(bx, by);
            const PlTemplate *tpl = block_template(h);
            int order[PL_TEMPLATE_MAX];

            block_order(h, n, tpl->count, order);

            for (int s = 0; s < tpl->count; s++) {
                double x, y, tw, th, cx, cy, vx, vy;
                double primary, secondary, score;
                gboolean overlap;

                if (bx == P.focus_bx && by == P.focus_by && s == P.focus_slot)
                    continue;

                slot_rect(bx, by, tpl, block_flip(h), s, &x, &y, &tw, &th);
                cx = x + tw * 0.5;
                cy = y + th * 0.5;
                vx = cx - fcx;
                vy = cy - fcy;

                if (dx > 0) {
                    if (vx <= 1.0) continue;
                    primary = vx;
                    secondary = fabs(vy);
                } else if (dx < 0) {
                    if (vx >= -1.0) continue;
                    primary = -vx;
                    secondary = fabs(vy);
                } else if (dy > 0) {
                    if (vy <= 1.0) continue;
                    primary = vy;
                    secondary = fabs(vx);
                } else {
                    if (vy >= -1.0) continue;
                    primary = -vy;
                    secondary = fabs(vx);
                }

                overlap = dx != 0
                    ? (y < P.focus_wy + P.focus_wh && y + th > P.focus_wy)
                    : (x < P.focus_wx + P.focus_ww && x + tw > P.focus_wx);

                score = primary + secondary * 1.6 - (overlap ? 1000000.0 : 0.0);

                if (score < best_score) {
                    best_score = score;
                    best_bx = bx;
                    best_by = by;
                    best_slot = s;
                    best_track = order[s];
                    found = TRUE;
                }
            }
        }
    }

    if (found)
        focus_set_instance(best_bx, best_by, best_slot, best_track, TRUE);
}

static void focus_page(gint dir)
{
    const APPconfig *cfg = config_get();
    int              n   = (int)cfg->n_tracks;
    double           w, h, fcx, fcy, target;
    double           best_score = 1e30;
    int              best_bx = 0, best_by = 0, best_slot = 0, best_track = 0;
    gboolean         found = FALSE;

    if (!P.open || P.closing || n <= 0 || dir == 0)
        return;

    P.pan_vx = P.pan_vy = 0.0;
    focus_sync_to_view();

    if (!P.focus_valid)
        return;

    view_size(&w, &h);
    fcx = P.focus_wx + P.focus_ww * 0.5;
    fcy = P.focus_wy + P.focus_wh * 0.5;
    target = fcy + (double)dir * MAX(160.0, h * 0.72);

    for (int oy = -2; oy <= 2; oy++) {
        for (int ox = -2; ox <= 2; ox++) {
            int bx = P.focus_bx + ox;
            int by = P.focus_by + oy;
            guint32 bh = block_hash(bx, by);
            const PlTemplate *tpl = block_template(bh);
            int order[PL_TEMPLATE_MAX];

            block_order(bh, n, tpl->count, order);

            for (int s = 0; s < tpl->count; s++) {
                double x, y, tw, th, cx, cy, score;

                if (bx == P.focus_bx && by == P.focus_by && s == P.focus_slot)
                    continue;

                slot_rect(bx, by, tpl, block_flip(bh), s, &x, &y, &tw, &th);
                cx = x + tw * 0.5;
                cy = y + th * 0.5;

                if (dir > 0 && cy <= fcy + 1.0)
                    continue;
                if (dir < 0 && cy >= fcy - 1.0)
                    continue;

                score = fabs(cy - target) + fabs(cx - fcx) * 1.2;

                if (score < best_score) {
                    best_score = score;
                    best_bx = bx;
                    best_by = by;
                    best_slot = s;
                    best_track = order[s];
                    found = TRUE;
                }
            }
        }
    }

    if (found)
        focus_set_instance(best_bx, best_by, best_slot, best_track, TRUE);
}

/* ---------------- 鼠标滚动 ---------------- */

void playlist_pan_begin(double x, double y)
{
    if (!P.open || P.closing)
        return;

    P.dragging = TRUE;
    P.drag_moved = FALSE;
    P.pan_vx = P.pan_vy = 0.0;
    P.drag_start_x = x;
    P.drag_start_y = y;
    P.drag_cam_x = P.cam_x;
    P.drag_cam_y = P.cam_y;
    P.drag_last_x = x;
    P.drag_last_y = y;
    P.drag_last_us = now_us();
    P.cam_tx = P.cam_x;
    P.cam_ty = P.cam_y;
}

void playlist_pan_update(double offset_x, double offset_y)
{
    gint64 now;
    double dt, nx, ny;

    if (!P.dragging)
        return;

    nx = P.drag_cam_x - offset_x;
    ny = P.drag_cam_y - offset_y;

    now = now_us();
    dt  = (double)(now - P.drag_last_us) / 1000000.0;

    if (dt > 0.001) {
        P.pan_vx = (nx - P.cam_x) / dt;
        P.pan_vy = (ny - P.cam_y) / dt;
        P.drag_last_us = now;
    }

    if (fabs(offset_x) > 5.0 || fabs(offset_y) > 5.0)
        P.drag_moved = TRUE;

    P.cam_x = CLAMP(nx, -1e7, 1e7);
    P.cam_y = CLAMP(ny, -1e7, 1e7);
    P.cam_tx = P.cam_x;
    P.cam_ty = P.cam_y;

    playlist_request_frame();
}

void playlist_pan_end(double offset_x, double offset_y)
{
    double speed;

    (void)offset_x;
    (void)offset_y;

    if (!P.dragging)
        return;

    P.dragging = FALSE;
    P.last_drag_end_us = now_us();

    if (!P.drag_moved) {
        P.pan_vx = P.pan_vy = 0.0;
        playlist_click(P.drag_start_x, P.drag_start_y);
        return;
    }

    speed = hypot(P.pan_vx, P.pan_vy);

    /* 拖到新区域后，把选中焦点落到视野内最近的磁贴上 */
    focus_sync_to_view();

    if (!config_animations() || speed < 120.0 ||
        now_us() - P.drag_last_us > 160000) {
        P.pan_vx = P.pan_vy = 0.0;
    } else if (speed > 5200.0) {
        P.pan_vx *= 5200.0 / speed;
        P.pan_vy *= 5200.0 / speed;
    }

    ui_animate();
    playlist_request_frame();
}

void playlist_pan_by(double dx, double dy)
{
    if (!P.open || P.closing)
        return;

    P.dragging = FALSE;
    P.pan_vx = P.pan_vy = 0.0;
    P.cam_tx += dx * 42.0;
    P.cam_ty += dy * 42.0;
    P.cam_tx = CLAMP(P.cam_tx, -1e7, 1e7);
    P.cam_ty = CLAMP(P.cam_ty, -1e7, 1e7);

    if (!config_animations()) {
        P.cam_x = P.cam_tx;
        P.cam_y = P.cam_ty;
    }

    ui_animate();
    playlist_request_frame();
}

gboolean playlist_was_dragging(void)
{
    return P.dragging ||
           (now_us() - P.last_drag_end_us < 260000);
}

/* ---------------- 绘制 ---------------- */

static void draw_placeholder(cairo_t *cr, gsize index, const AudioTrack *track,
                             double x, double y, double size, double radius)
{
    const Palette *pal = covercache_palette(index);
    double         r, g, b;
    TextStyle      note = TEXT_STYLE(FONT_MONO, size * 0.24, PANGO_WEIGHT_NORMAL,
                                     0.0, WHITE(0.16));

    if (pal != NULL && pal->has) {
        r = pal->seed_r;
        g = pal->seed_g;
        b = pal->seed_b;
    } else {
        hash_color(track, &r, &g, &b);
    }

    cairo_save(cr);
    path_round_rect(cr, x, y, size, size, radius);
    cairo_clip(cr);

    {
        cairo_pattern_t *pat = cairo_pattern_create_linear(x, y, x, y + size);

        cairo_pattern_add_color_stop_rgba(pat, 0.0, r * 1.05, g * 1.05, b * 1.05, 0.95);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, r * 0.26, g * 0.26, b * 0.26, 0.95);
        cairo_set_source(cr, pat);
        cairo_paint(cr);
        cairo_pattern_destroy(pat);
    }

    text_draw(cr, "♪", &note, x + size * 0.5, y + size * 0.34,
              TEXT_CENTER, 0.0);

    cairo_restore(cr);
}

static void draw_cover_image(cairo_t *cr, gsize index, const AudioTrack *track,
                             double x, double y, double w, double h, double radius,
                             gboolean full_cover)
{
    cairo_surface_t *surface = covercache_surface(index);
    double           reveal  = full_cover ? 1.0 : covercache_reveal(index);
    int              px      = ((int)ceil(MAX(w, h)) + 7) & ~7;

    cairo_save(cr);
    path_round_rect(cr, x, y, w, h, radius);
    cairo_clip(cr);
    draw_placeholder(cr, index, track, x, y, MAX(w, h), radius);
    cairo_restore(cr);

    if (surface == NULL || reveal <= 0.0)
        return;

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_save(cr);
    path_round_rect(cr, x, y, w, h, radius);
    cairo_clip(cr);

    {
        double iw = (double)cairo_image_surface_get_width(surface);
        double ih = (double)cairo_image_surface_get_height(surface);
        double scale, dw, dh;

        (void)px;

        if (iw > 0.0 && ih > 0.0) {
            scale = MAX(w / iw, h / ih);
            dw = iw * scale;
            dh = ih * scale;

            cairo_translate(cr, x + (w - dw) * 0.5, y + (h - dh) * 0.5);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, surface, 0.0, 0.0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint_with_alpha(cr, reveal);
        }
    }

    cairo_restore(cr);
}

static void draw_tile_content(cairo_t *cr, gsize track_index,
                              double x, double y, double w, double h,
                              gboolean full_cover)
{
    const APPconfig  *cfg   = config_get();
    const AudioTrack *track = config_track_at(cfg, track_index);
    const Palette    *pal   = covercache_palette(track_index);
    g_autofree gchar *title = config_track_title(track);
    Platform          platform = track_platform(track);
    double            radius = 2.0;
    double            pad = clampd(w * 0.05, 8.0, 18.0);

    draw_cover_image(cr, track_index, track, x, y, w, h, radius, full_cover);

    /* 海报叠色：封面主色以极低透明度铺一层，整面墙的色调会更统一 */
    if (pal != NULL && pal->has) {
        cairo_save(cr);
        path_round_rect(cr, x, y, w, h, radius);
        cairo_clip(cr);

        {
            cairo_pattern_t *tint = cairo_pattern_create_linear(x, y, x, y + h);

            cairo_pattern_add_color_stop_rgba(tint, 0.0, pal->seed_r, pal->seed_g,
                                              pal->seed_b, 0.10);
            cairo_pattern_add_color_stop_rgba(tint, 0.55, pal->alt_r, pal->alt_g,
                                              pal->alt_b, 0.025);
            cairo_pattern_add_color_stop_rgba(tint, 1.0, pal->deep_r, pal->deep_g,
                                              pal->deep_b, 0.06);
            cairo_set_source(cr, tint);
            cairo_paint(cr);
            cairo_pattern_destroy(tint);
        }

        cairo_restore(cr);
    }

    if (w > 88.0 && h > 72.0) {
        double title_px  = clampd(w * 0.060, 11.0, 26.0);
        double artist_px = clampd(w * 0.035, 8.0, 13.0);
        double text_w    = w - pad * 2.0;
        TextStyle title_style = TEXT_STYLE(FONT_MONO, title_px, PANGO_WEIGHT_BOLD,
                                           0.0, WHITE(0.96));
        TextStyle artist_style = TEXT_STYLE(FONT_MONO, artist_px, PANGO_WEIGHT_NORMAL,
                                            0.02, WHITE(0.58));
        double title_h = text_block_height(cr, title != NULL ? title : "",
                                           &title_style, text_w, 2);
        double artist_h = text_height(cr,
                                      track != NULL && track->artist != NULL
                                          ? track->artist : "", &artist_style);
        double artist_y = y + h - pad - artist_h;
        double title_y  = artist_y - 2.0 - title_h;

        if (title_y < y + h * 0.38)
            title_y = y + h * 0.38;

        text_draw_block(cr, title != NULL ? title : "", &title_style,
                        x + pad, title_y, TEXT_LEFT, text_w, 2);

        if (track != NULL && track->artist != NULL && *track->artist != '\0')
            text_draw(cr, track->artist, &artist_style, x + pad, artist_y,
                      TEXT_LEFT, text_w);

        if (w > 150.0) {
            const gchar *name = platform != PLATFORM_UNKNOWN
                ? platform_name(platform)
                : (track != NULL && track->link_remote ? "LINK" : "LOCAL");
            TextStyle chip = TEXT_STYLE(FONT_MONO, clampd(w * 0.045, 7.0, 9.0),
                                        PANGO_WEIGHT_NORMAL, 0.06, WHITE(0.62));
            double cw = text_width(cr, name, &chip);
            double ch = chip.px * 1.35 + 6.0;
            double cx = x + w - cw - 18.0;
            double cy = y + 8.0;

            color_set(cr, RGBA(0.0, 0.0, 0.0, 0.42));
            path_round_rect(cr, cx, cy, cw + 10.0, ch, 2.0);
            cairo_fill(cr);
            text_draw(cr, name, &chip, cx + 5.0, cy + 3.0, TEXT_LEFT, cw + 2.0);
        }
    }
}

/* 静态磁贴内容缓存：滚动/选中动画时只 blit，不再每帧排 Pango、缩放封面。 */
typedef struct {
    cairo_surface_t *surface;
    gsize            track;
    int              w, h;
    int              scale;      /* 1 = 逻辑像素，2 = HiDPI 设备像素渲染 */
    gint64           version;
    gint64           used_us;
    gboolean         used;
} PlTileCache;

static PlTileCache g_tile_cache[PL_TILE_CACHE_MAX];

#define PL_OVERLAY_CACHE_MAX 16

typedef struct {
    cairo_surface_t *surface;
    gsize            track;
    int              w, h;
    int              scale;
    gint64           version;
    guint            flags;
    gint64           used_us;
    gboolean         used;
} PlOverlayCache;

static PlOverlayCache g_overlay_cache[PL_OVERLAY_CACHE_MAX];

/* 普通磁贴的细边框；和磁贴内容一起缓存进纹理。 */
static void draw_normal_border(cairo_t *cr, double x, double y, double w, double h)
{
    color_set(cr, WHITE(0.05));
    cairo_set_line_width(cr, 1.0);
    path_round_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, 2.0);
    cairo_stroke(cr);
}

static void tile_cache_clear(void)
{
    for (int i = 0; i < PL_TILE_CACHE_MAX; i++) {
        g_clear_pointer(&g_tile_cache[i].surface, cairo_surface_destroy);
        g_tile_cache[i].used = FALSE;
    }

    for (int i = 0; i < PL_OVERLAY_CACHE_MAX; i++) {
        g_clear_pointer(&g_overlay_cache[i].surface, cairo_surface_destroy);
        g_overlay_cache[i].used = FALSE;
    }
}

static cairo_surface_t *tile_cache_find(gsize track, int w, int h,
                                       gint64 version, int scale)
{
    if (w < 8 || h < 8)
        return NULL;

    scale = CLAMP(scale, 1, 3);

    for (int i = 0; i < PL_TILE_CACHE_MAX; i++) {
        if (g_tile_cache[i].used &&
            g_tile_cache[i].track == track &&
            g_tile_cache[i].w == w && g_tile_cache[i].h == h &&
            g_tile_cache[i].scale == scale &&
            g_tile_cache[i].version == version) {
            g_tile_cache[i].used_us = now_us();
            return g_tile_cache[i].surface;
        }
    }

    return NULL;
}

static cairo_surface_t *tile_cache_get(gsize track, int w, int h, gint64 version,
                                       int scale)
{
    cairo_surface_t *surface;
    cairo_t         *cr;
    int              slot = -1;

    if (w < 8 || h < 8)
        return NULL;

    scale = CLAMP(scale, 1, 3);

    surface = tile_cache_find(track, w, h, version, scale);
    if (surface != NULL)
        return surface;

    for (int i = 0; i < PL_TILE_CACHE_MAX; i++) {
        if (!g_tile_cache[i].used) {
            slot = i;
            break;
        }
        if (slot < 0 || g_tile_cache[i].used_us < g_tile_cache[slot].used_us)
            slot = i;
    }

    if (slot < 0)
        return NULL;

    g_clear_pointer(&g_tile_cache[slot].surface, cairo_surface_destroy);

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * scale, h * scale);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        g_tile_cache[slot].used = FALSE;
        return NULL;
    }

    cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    draw_tile_content(cr, track, 0.0, 0.0, (double)w, (double)h, TRUE);
    draw_normal_border(cr, 0.0, 0.0, (double)w, (double)h);
    cairo_destroy(cr);

    g_tile_cache[slot].surface = surface;
    g_tile_cache[slot].track   = track;
    g_tile_cache[slot].w       = w;
    g_tile_cache[slot].h       = h;
    g_tile_cache[slot].scale   = scale;
    g_tile_cache[slot].version = version;
    g_tile_cache[slot].used_us = now_us();
    g_tile_cache[slot].used    = TRUE;

    return surface;
}

/* ---------------- 磁贴异步预热 ----------------
 * 2K / 4K 上一张磁贴要重排 Pango、缩放封面，单张 2-4ms。整屏 40+ 张
 * 同步渲染会把打开墙卡住 100ms 以上，快速拖动时也会丢帧。这里把
 * “生成 cairo 磁贴”放进低优先级空闲任务，每轮只吃几毫秒，生成完
 * 再通知 wallgl 上传纹理。 */

typedef struct {
    gsize track;
    int   w, h, scale;
} PlTileJob;

static GQueue g_tile_jobs = G_QUEUE_INIT;
static guint  g_tile_job_id;

static gboolean tile_job_step(gpointer data);

void playlist_tile_request(gsize track, int w, int h, int scale)
{
    gint64 version;

    if (w < 8 || h < 8)
        return;

    version = covercache_version(track);

    if (tile_cache_find(track, w, h, version, scale) != NULL)
        return;

    for (GList *l = g_tile_jobs.head; l != NULL; l = l->next) {
        PlTileJob *job = l->data;

        if (job->track == track && job->w == w && job->h == h &&
            job->scale == scale)
            return;
    }

    if (g_tile_jobs.length >= 512)
        return;

    {
        PlTileJob *job = g_new(PlTileJob, 1);

        job->track = track;
        job->w     = w;
        job->h     = h;
        job->scale = scale;
        g_queue_push_tail(&g_tile_jobs, job);
    }

    if (g_tile_job_id == 0)
        g_tile_job_id = g_timeout_add(4, tile_job_step, NULL);
}

cairo_surface_t *playlist_tile_surface_cached(gsize track, int w, int h, int scale)
{
    return tile_cache_find(track, w, h, covercache_version(track), scale);
}

static gboolean tile_job_step(gpointer data)
{
    gboolean did = FALSE;
    gint64   start = now_us();

    (void)data;

    /* 每轮最多占用 ~3ms，把剩下的时间还给 60fps 动画。 */
    while (!g_queue_is_empty(&g_tile_jobs)) {
        PlTileJob *job = g_queue_pop_head(&g_tile_jobs);
        gint64     version = covercache_version(job->track);

        if (tile_cache_find(job->track, job->w, job->h, version,
                            job->scale) == NULL) {
            tile_cache_get(job->track, job->w, job->h, version, job->scale);
            did = TRUE;
        }

        g_free(job);

        if (now_us() - start >= 3000)
            break;
    }

    if (did)
        playlist_request_frame();

    if (g_queue_is_empty(&g_tile_jobs)) {
        g_tile_job_id = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void tile_jobs_clear(void)
{
    if (g_tile_job_id != 0) {
        g_source_remove(g_tile_job_id);
        g_tile_job_id = 0;
    }

    while (!g_queue_is_empty(&g_tile_jobs))
        g_free(g_queue_pop_head(&g_tile_jobs));
}

/* 磁贴上的标记层：当前曲目强调条 / ♪、选中描边、悬停边框。
 * 单独抬出来，cairo 回退和 GL 纹理共用同一套绘制。 */
static void draw_tile_decoration(cairo_t *cr, gsize track_index,
                                 double x, double y, double w, double h,
                                 gboolean selected, gboolean hovered,
                                 gboolean current)
{
    const Palette *pal = covercache_palette(track_index);
    double         radius = 2.0;

    if (current) {
        Rgba accent = (pal != NULL && pal->has)
            ? RGBA(pal->seed_r * 0.65 + 0.35, pal->seed_g * 0.65 + 0.35,
                   pal->seed_b * 0.65 + 0.35, 1.0)
            : WHITE(0.85);
        TextStyle note = TEXT_STYLE(FONT_MONO, clampd(w * 0.055, 8.0, 11.0),
                                    PANGO_WEIGHT_BOLD, 0.0, accent);

        color_set(cr, accent);
        cairo_rectangle(cr, x + 1.0, y + 1.0, 4.0, h - 2.0);
        cairo_fill(cr);

        color_set(cr, RGBA(0.0, 0.0, 0.0, 0.42));
        path_round_rect(cr, x + 9.0, y + 9.0, 22.0, 22.0, 2.0);
        cairo_fill(cr);
        text_draw(cr, "♪", &note, x + 20.0, y + 13.0, TEXT_CENTER, 0.0);
    }

    if (selected) {
        color_set(cr, RGBA(1.0, 1.0, 1.0, 0.10));
        cairo_set_line_width(cr, 7.0);
        path_round_rect(cr, x - 3.5, y - 3.5, w + 7.0, h + 7.0, radius + 3.0);
        cairo_stroke(cr);

        color_set(cr, RGBA(0.0, 0.0, 0.0, 0.62));
        cairo_set_line_width(cr, 5.0);
        path_round_rect(cr, x - 1.5, y - 1.5, w + 3.0, h + 3.0, radius + 1.5);
        cairo_stroke(cr);

        color_set(cr, WHITE(0.98));
        cairo_set_line_width(cr, 3.0);
        path_round_rect(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0, radius);
        cairo_stroke(cr);
    } else if (hovered) {
        color_set(cr, WHITE(0.36));
        cairo_set_line_width(cr, 1.0);
        path_round_rect(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0, radius);
        cairo_stroke(cr);
    }
}

static void draw_tile(cairo_t *cr, gsize track_index, double x, double y,
                      double w, double h, gboolean selected, gboolean hovered,
                      gboolean current)
{
    const Palette *pal = covercache_palette(track_index);
    double         radius = 2.0;
    double         pop = 1.0;
    double         reveal = covercache_reveal(track_index);

    if (selected && config_animations()) {
        double age = (double)(now_us() - P.sel_us) / 1000000.0;

        pop = 1.0 + 0.028 * exp(-age * 12.0);
    }

    if (pop != 1.0) {
        cairo_save(cr);
        cairo_translate(cr, x + w * 0.5, y + h * 0.5);
        cairo_scale(cr, pop, pop);
        cairo_translate(cr, -(x + w * 0.5), -(y + h * 0.5));
    }

    /* 选中磁贴：先用封面主色在周围垫一层柔光 */
    if (selected && pal != NULL && pal->has) {
        double           cx = x + w * 0.5, cy = y + h * 0.5;
        double           rr = MAX(w, h) * 0.68;
        cairo_pattern_t *glow = cairo_pattern_create_radial(cx, cy, 0.0, cx, cy, rr);

        cairo_pattern_add_color_stop_rgba(glow, 0.0, pal->seed_r, pal->seed_g,
                                          pal->seed_b, 0.18);
        cairo_pattern_add_color_stop_rgba(glow, 0.55, pal->seed_r, pal->seed_g,
                                          pal->seed_b, 0.06);
        cairo_pattern_add_color_stop_rgba(glow, 1.0, pal->seed_r, pal->seed_g,
                                          pal->seed_b, 0.0);
        cairo_set_source(cr, glow);
        cairo_paint(cr);
        cairo_pattern_destroy(glow);
    }

    {
        gint64           version = covercache_version(track_index);
        cairo_surface_t *cover   = covercache_surface(track_index);
        cairo_surface_t *cached  = tile_cache_get(track_index,
                                                  (int)ceil(w), (int)ceil(h),
                                                  version, 1);

        if (cached != NULL) {
            if (cover != NULL && reveal < 0.999) {
                /* 封面正在淡入：底下先铺占位色，再把缓存整块按 alpha 叠上去 */
                cairo_save(cr);
                path_round_rect(cr, x, y, w, h, radius);
                cairo_clip(cr);
                draw_placeholder(cr, track_index,
                                 config_track_at(config_get(), track_index),
                                 x, y, MAX(w, h), radius);
                cairo_restore(cr);
            }

            cairo_set_source_surface(cr, cached, x, y);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);

            /* 封面还没加载好时，缓存本身就是“占位底 + 标题”的磁贴；
             * 不能乘 reveal（0）否则整块磁贴会隐形。 */
            if (cover == NULL || reveal >= 0.999)
                cairo_paint(cr);
            else
                cairo_paint_with_alpha(cr, reveal);
        } else {
            draw_tile_content(cr, track_index, x, y, w, h, FALSE);
            draw_normal_border(cr, x, y, w, h);
        }
    }

    draw_tile_decoration(cr, track_index, x, y, w, h, selected, hovered, current);

    if (pop != 1.0)
        cairo_restore(cr);
}

/* GL 层用的标记纹理：只有选中 / 悬停 / 当前曲目时才生成。
 * 四周留 4px 给选中描边的外描边。 */
cairo_surface_t *playlist_tile_overlay(gsize track, int w, int h,
                                       gboolean selected, gboolean hovered,
                                       gboolean current, int scale)
{
    guint           flags = (selected ? 1u : 0u) | (hovered ? 2u : 0u) |
                            (current ? 4u : 0u);
    gint64          version = covercache_version(track);
    gint64          now = now_us();
    cairo_surface_t *surface;
    int             slot = -1;
    cairo_t        *cr;

    if (w < 8 || h < 8 || flags == 0)
        return NULL;

    scale = CLAMP(scale, 1, 3);

    for (int i = 0; i < PL_OVERLAY_CACHE_MAX; i++) {
        if (g_overlay_cache[i].used && g_overlay_cache[i].track == track &&
            g_overlay_cache[i].w == w && g_overlay_cache[i].h == h &&
            g_overlay_cache[i].scale == scale &&
            g_overlay_cache[i].version == version &&
            g_overlay_cache[i].flags == flags) {
            g_overlay_cache[i].used_us = now;
            return g_overlay_cache[i].surface;
        }

        if (!g_overlay_cache[i].used) {
            slot = i;
            break;
        }

        if (slot < 0 || g_overlay_cache[i].used_us < g_overlay_cache[slot].used_us)
            slot = i;
    }

    if (slot < 0)
        return NULL;

    g_clear_pointer(&g_overlay_cache[slot].surface, cairo_surface_destroy);

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                         (w + 8) * scale, (h + 8) * scale);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        g_overlay_cache[slot].used = FALSE;
        return NULL;
    }

    cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, 4.0, 4.0);
    draw_tile_decoration(cr, track, 0.0, 0.0, (double)w, (double)h,
                         selected, hovered, current);
    cairo_destroy(cr);

    g_overlay_cache[slot].surface = surface;
    g_overlay_cache[slot].track   = track;
    g_overlay_cache[slot].w       = w;
    g_overlay_cache[slot].h       = h;
    g_overlay_cache[slot].scale   = scale;
    g_overlay_cache[slot].version = version;
    g_overlay_cache[slot].flags   = flags;
    g_overlay_cache[slot].used_us = now;
    g_overlay_cache[slot].used    = TRUE;

    return surface;
}

/* 当前应该拿来给整屏置色的封面：优先 focus，还没就位时先用正在播放的那首。 */
static const Palette *background_palette(void)
{
    if (P.bg_has && P.bg_pal.has)
        return &P.bg_pal;

    if (app.palette.has)
        return &app.palette;

    return NULL;
}

static void background_color(double *r, double *g, double *b)
{
    const Palette *pal = background_palette();
    double         pr = 0.55, pg = 0.68, pb = 0.92;

    if (pal != NULL) {
        pr = pal->seed_r;
        pg = pal->seed_g;
        pb = pal->seed_b;
    }

    /* 底色只给一点点色相，真正的置色交给两团柔光（和 Folia 的 root 一样） */
    *r = 0.012 + pr * 0.035;
    *g = 0.013 + pg * 0.035;
    *b = 0.018 + pb * 0.035;
}

static void draw_tint(cairo_t *cr, double w, double h)
{
    const Palette *pal = background_palette();
    double         diag;

    if (pal == NULL)
        return;

    diag = hypot(w, h);

    {
        cairo_pattern_t *p = cairo_pattern_create_radial(
            w * 0.14, h * 0.12, 0.0, w * 0.14, h * 0.12, diag * 0.30);

        cairo_pattern_add_color_stop_rgba(p, 0.00, pal->seed_r, pal->seed_g,
                                          pal->seed_b, 0.18);
        cairo_pattern_add_color_stop_rgba(p, 0.55, pal->seed_r, pal->seed_g,
                                          pal->seed_b, 0.07);
        cairo_pattern_add_color_stop_rgba(p, 1.00, pal->seed_r, pal->seed_g,
                                          pal->seed_b, 0.0);
        cairo_set_source(cr, p);
        cairo_paint(cr);
        cairo_pattern_destroy(p);
    }

    {
        cairo_pattern_t *p = cairo_pattern_create_radial(
            w * 0.86, h * 0.82, 0.0, w * 0.86, h * 0.82, diag * 0.32);

        cairo_pattern_add_color_stop_rgba(p, 0.00, pal->alt_r, pal->alt_g,
                                          pal->alt_b, 0.20);
        cairo_pattern_add_color_stop_rgba(p, 0.55, pal->alt_r, pal->alt_g,
                                          pal->alt_b, 0.08);
        cairo_pattern_add_color_stop_rgba(p, 1.00, pal->alt_r, pal->alt_g,
                                          pal->alt_b, 0.0);
        cairo_set_source(cr, p);
        cairo_paint(cr);
        cairo_pattern_destroy(p);
    }
}

static void draw_background(cairo_t *cr, double w, double h)
{
    double r, g, b;

    background_color(&r, &g, &b);

    cairo_set_source_rgba(cr, r, g, b, 0.88);
    cairo_rectangle(cr, 0.0, 0.0, w, h);
    cairo_fill(cr);

    /* 两团柔光铺满整屏：SONGS 那一行也会被置色 */
    draw_tint(cr, w, h);
}

static void draw_header(cairo_t *cr, double w, double h, gsize n_tracks)
{
    double    pad = P.pad_x;
    double    y   = clampd(h * 0.055, 24.0, 70.0);
    TextStyle title = TEXT_STYLE(FONT_MONO, clampd(h * 0.050, 22.0, 56.0),
                                 PANGO_WEIGHT_BOLD, 0.10, WHITE(0.94));
    TextStyle count = TEXT_STYLE(FONT_MONO, clampd(h * 0.013, 9.0, 14.0),
                                 PANGO_WEIGHT_NORMAL, 0.10, col_dim);
    TextStyle hint  = TEXT_STYLE(FONT_MONO, clampd(h * 0.012, 8.0, 13.0),
                                 PANGO_WEIGHT_NORMAL, 0.08, col_hint);
    g_autofree gchar *count_text = g_strdup_printf("%zu TRACK%s", n_tracks,
                                                   n_tracks == 1 ? "" : "S");
    g_autofree gchar *hint_text = g_strdup_printf("%s  CLOSE",
                                                  keymap_label(KEY_PLAYLIST));
    double hint_w = text_width(cr, hint_text, &hint);

    {
        const Palette *pal = background_palette();
        double         vr = 0.006, vg = 0.007, vb = 0.012;

        /* 和底栏一样：整条实心颜色衬 + 一条细线收边。
         * 不做淡出，否则渐变尾段会变成一条“既不是衬、也不是墙”的灰带。 */
        if (pal != NULL) {
            vr = 0.004 + pal->seed_r * 0.07;
            vg = 0.005 + pal->seed_g * 0.07;
            vb = 0.009 + pal->seed_b * 0.07;
        }

        color_set(cr, RGBA(vr, vg, vb, 0.90));
        cairo_rectangle(cr, 0.0, 0.0, w, P.header_h);
        cairo_fill(cr);

        color_set(cr, WHITE(0.08));
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, 0.0, P.header_h + 0.5);
        cairo_line_to(cr, w, P.header_h + 0.5);
        cairo_stroke(cr);
    }

    text_draw(cr, "SONGS", &title, pad, y, TEXT_LEFT, 0.0);
    text_draw(cr, count_text, &count, pad, y + title.px * 1.02, TEXT_LEFT, 0.0);
    text_draw(cr, hint_text, &hint, w - pad - hint_w, y + title.px * 0.36,
              TEXT_LEFT, 0.0);
}

static void draw_bottom(cairo_t *cr, double w, double h, gsize n_tracks)
{
    const APPconfig  *cfg;
    const AudioTrack *track;
    g_autofree gchar *title = NULL;
    g_autofree gchar *artist = NULL;
    double            bar_y = h - P.bottom_h;
    double            pad = P.pad_x;
    double            time_px = clampd(h * 0.011, 8.0, 12.0);
    TextStyle         time_style = TEXT_STYLE(FONT_MONO, time_px, PANGO_WEIGHT_NORMAL,
                                              0.06, col_dim);
    TextStyle         hint_style = TEXT_STYLE(FONT_MONO, clampd(h * 0.010, 8.0, 11.0),
                                              PANGO_WEIGHT_NORMAL, 0.04, col_hint);
    TextStyle         title_style = TEXT_STYLE(FONT_MONO, clampd(h * 0.014, 9.0, 15.0),
                                               PANGO_WEIGHT_BOLD, 0.02, WHITE(0.82));
    TextStyle         artist_style = TEXT_STYLE(FONT_MONO, clampd(h * 0.010, 8.0, 11.0),
                                                PANGO_WEIGHT_NORMAL, 0.02, col_dim);
    double            position = media_position(&app.media);
    double            duration = media_duration(&app.media);
    g_autofree gchar *left  = format_time(position);
    g_autofree gchar *right = duration > 0.0 ? format_time(duration) : g_strdup("--:--");
    double            area_w = clampd(w * 0.26, 220.0, 460.0);
    double            area_x = w - pad - area_w;
    double            left_w = text_width(cr, left, &time_style);
    double            right_w = text_width(cr, right, &time_style);
    double            bar_x = area_x + left_w + 12.0;
    double            bar_w = MAX(40.0, area_w - left_w - right_w - 24.0);
    double            fill;
    int               shown = app.track >= 0 ? app.track : P.focus_track;
    g_autofree gchar *hint = g_strdup_printf(
        "DRAG / WHEEL  PAN      ← ↑ ↓ →  SELECT      ENTER  PLAY      %s  CLOSE",
        keymap_label(KEY_PLAYLIST));

    cfg   = config_get();
    track = config_track_at(cfg, (gsize)MAX(shown, 0));

    if (n_tracks > 0 && track != NULL) {
        title = config_track_title(track);

        if (track->artist != NULL && *track->artist != '\0')
            artist = g_strdup(track->artist);
    }

    {
        const Palette *pal = background_palette();
        double         br = 0.006, bg = 0.007, bb = 0.012;

        if (pal != NULL) {
            br = 0.004 + pal->seed_r * 0.07;
            bg = 0.005 + pal->seed_g * 0.07;
            bb = 0.009 + pal->seed_b * 0.07;
        }

        color_set(cr, RGBA(br, bg, bb, 0.90));
    }
    cairo_rectangle(cr, 0.0, bar_y, w, P.bottom_h);
    cairo_fill(cr);

    color_set(cr, WHITE(0.08));
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0.0, bar_y + 0.5);
    cairo_line_to(cr, w, bar_y + 0.5);
    cairo_stroke(cr);

    text_draw(cr, "♪", &title_style, pad, bar_y + P.bottom_h * 0.26,
              TEXT_LEFT, 0.0);
    text_draw(cr, title != NULL ? title : "—", &title_style,
              pad + 18.0, bar_y + P.bottom_h * 0.26, TEXT_LEFT,
              MAX(80.0, area_x - pad - 28.0));

    if (artist != NULL)
        text_draw(cr, artist, &artist_style, pad + 18.0,
                  bar_y + P.bottom_h * 0.26 + title_style.px * 1.35,
                  TEXT_LEFT, MAX(80.0, area_x - pad - 28.0));

    text_draw(cr, left, &time_style, area_x,
              bar_y + P.bottom_h * 0.28, TEXT_LEFT, 0.0);
    text_draw(cr, right, &time_style, area_x + area_w - right_w,
              bar_y + P.bottom_h * 0.28, TEXT_LEFT, 0.0);

    color_set(cr, WHITE(0.12));
    path_round_rect(cr, bar_x, bar_y + P.bottom_h * 0.30, bar_w, 3.0, 1.5);
    cairo_fill(cr);

    fill = duration > 0.0 ? bar_w * clampd(position / duration, 0.0, 1.0) : 0.0;

    if (fill > 0.5) {
        const Palette *pal = covercache_palette((gsize)MAX(shown, 0));

        if (pal != NULL && pal->has)
            color_set(cr, RGBA(pal->seed_r * 0.6 + 0.4, pal->seed_g * 0.6 + 0.4,
                               pal->seed_b * 0.6 + 0.4, 0.82));
        else
            color_set(cr, WHITE(0.62));
        path_round_rect(cr, bar_x, bar_y + P.bottom_h * 0.30, fill, 3.0, 1.5);
        cairo_fill(cr);
    }

    if (P.bottom_h > 62.0)
        text_draw(cr, hint, &hint_style, w * 0.5,
                  bar_y + P.bottom_h - hint_style.px * 1.8,
                  TEXT_CENTER, MIN(w * 0.88, 1100.0));
}

static void draw_empty(cairo_t *cr, double w, double h)
{
    TextStyle title = TEXT_STYLE(FONT_MONO, clampd(h * 0.032, 16.0, 34.0),
                                 PANGO_WEIGHT_BOLD, 0.10, WHITE(0.72));
    TextStyle hint  = TEXT_STYLE(FONT_MONO, clampd(h * 0.014, 9.0, 14.0),
                                 PANGO_WEIGHT_NORMAL, 0.04, col_hint);
    double    cx    = w * 0.5;
    double    cy    = h * 0.48;

    text_draw(cr, "PLAYLIST IS EMPTY", &title, cx, cy - title.px, TEXT_CENTER,
              MIN(w * 0.78, 900.0));
    text_draw(cr, "add [audioN] entries to config.ini", &hint,
              cx, cy + title.px * 0.9, TEXT_CENTER, MIN(w * 0.78, 900.0));
}

static void draw_wall(cairo_t *cr, double w, double h)
{
    const APPconfig *cfg = config_get();
    int              n   = (int)cfg->n_tracks;
    double           overscan = P.cell * 0.8;
    int              bx0, bx1, by0, by1;
    gboolean         focus_drawn = FALSE;
    double           fx = 0.0, fy = 0.0, fw = 0.0, fh = 0.0;

    P.n_hits = 0;

    if (n <= 0)
        return;

    bx0 = (int)floor((P.cam_x - overscan) / P.block_w);
    bx1 = (int)floor((P.cam_x + w + overscan) / P.block_w);
    by0 = (int)floor((P.cam_y - overscan) / P.block_h);
    by1 = (int)floor((P.cam_y + h + overscan) / P.block_h);

    for (int by = by0; by <= by1; by++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            guint32 bh = block_hash(bx, by);
            const PlTemplate *tpl = block_template(bh);
            int flip = block_flip(bh);
            int order[PL_TEMPLATE_MAX];

            block_order(bh, n, tpl->count, order);

            for (int s = 0; s < tpl->count; s++) {
                double wx, wy, ww, wh, x, y;
                int    track = order[s];
                gboolean selected, hovered;

                slot_rect(bx, by, tpl, flip, s, &wx, &wy, &ww, &wh);
                x = wx - P.cam_x;
                y = wy - P.cam_y;

                if (x + ww < -overscan || x > w + overscan ||
                    y + wh < -overscan || y > h + overscan)
                    continue;

                selected = P.focus_valid && bx == P.focus_bx &&
                           by == P.focus_by && s == P.focus_slot;
                hovered  = P.hover_valid && bx == P.hover_bx &&
                           by == P.hover_by && s == P.hover_slot;

                if (selected) {
                    focus_drawn = TRUE;
                    fx = x;
                    fy = y;
                    fw = ww;
                    fh = wh;
                } else {
                    draw_tile(cr, (gsize)track, x, y, ww, wh,
                              FALSE, hovered, FALSE);
                }

                if (P.n_hits < PL_MAX_HITS) {
                    PlHit *hit = &P.hit[P.n_hits++];

                    hit->track = track;
                    hit->bx = bx;
                    hit->by = by;
                    hit->slot = s;
                    hit->x = x;
                    hit->y = y;
                    hit->w = ww;
                    hit->h = wh;
                }
            }
        }
    }


    if (focus_drawn) {
        /* 选中时把周围压暗一点，focus 磁贴更突出 */
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
        cairo_rectangle(cr, 0.0, P.header_h, w, MAX(0.0, h - P.header_h - P.bottom_h));
        cairo_fill(cr);

        draw_tile(cr, (gsize)P.focus_track, fx, fy, fw, fh,
                  TRUE, P.hover_valid &&
                  P.hover_bx == P.focus_bx && P.hover_by == P.focus_by &&
                  P.hover_slot == P.focus_slot,
                  P.focus_track == app.track);
    }
}

static void warm_tile_cache(double w, double h)
{
    const APPconfig *cfg = config_get();
    int              n   = (int)cfg->n_tracks;
    double           overscan = P.cell * 0.8;

    if (n <= 0)
        return;

    for (int by = (int)floor((P.cam_y - overscan) / P.block_h);
         by <= (int)floor((P.cam_y + h + overscan) / P.block_h); by++) {
        for (int bx = (int)floor((P.cam_x - overscan) / P.block_w);
             bx <= (int)floor((P.cam_x + w + overscan) / P.block_w); bx++) {
            guint32 bh = block_hash(bx, by);
            const PlTemplate *tpl = block_template(bh);
            int order[PL_TEMPLATE_MAX];

            block_order(bh, n, tpl->count, order);

            for (int s = 0; s < tpl->count; s++) {
                double wx, wy, ww, wh;

                slot_rect(bx, by, tpl, block_flip(bh), s, &wx, &wy, &ww, &wh);

                if (wx + ww < P.cam_x - overscan || wx > P.cam_x + w + overscan ||
                    wy + wh < P.cam_y - overscan || wy > P.cam_y + h + overscan)
                    continue;

                covercache_surface((gsize)order[s]);
                playlist_tile_request((gsize)order[s],
                                      (int)ceil(ww), (int)ceil(wh),
                                      wallgl_ready() ? wallgl_tile_scale() : 1);
            }
        }
    }

    /* 选中 / focus 的那一张先同步画出来，进场第一帧就有内容；
     * 其余磁贴由低优先级队列补上。 */
    if (P.focus_valid) {
        int scale = wallgl_ready() ? wallgl_tile_scale() : 1;

        tile_cache_get((gsize)P.focus_track,
                       (int)ceil(P.focus_ww), (int)ceil(P.focus_wh),
                       covercache_version((gsize)P.focus_track), scale);
    }
}

/* ---------------- 对外接口 ---------------- */

void playlist_open_page(void)
{
    const APPconfig *cfg = config_get();
    gint64           t0, t_covers, t_tiles;

    if (P.open || P.closing)
        return;

    t0 = now_us();

    covercache_sync();

    P.open        = TRUE;
    P.closing     = FALSE;
    P.vis         = 0.0;
    P.gl_chrome   = FALSE;
    P.frame_dirty = TRUE;
    P.last_track  = app.track;
    P.seed        = (guint32)g_random_int() | 1u;
    P.dragging    = FALSE;
    P.drag_moved  = FALSE;
    P.pan_vx      = P.pan_vy = 0.0;
    P.last_drag_end_us = 0;
    P.hover_valid = FALSE;
    P.n_hits      = 0;
    P.bg_has      = FALSE;
    P.bg_moving   = FALSE;
    g_clear_pointer(&P.message, g_free);

    {
        double w, h;

        view_size(&w, &h);
        if (w > 0.0 && h > 0.0)
            update_metrics(w, h);
        else
            update_metrics((double)cfg->width, (double)cfg->height);
    }

    {
        int start_bx = (int)(g_random_int() % 5) - 2;
        int start_by = (int)(g_random_int() % 5) - 2;

        focus_find_track(app.track >= 0 ? app.track : 0, start_bx, start_by, 3);
    }

    /* 封面异步加载：这里不再同步解码所有 track，打开墙不会因为封面 / 调色板
     * 解码而卡住；可见磁贴先用占位色，封面到了再自动刷新。 */
    t_covers = now_us();

    focus_center_camera(TRUE);

    {
        double vw, vh;

        view_size(&vw, &vh);
        warm_tile_cache(vw, vh);
    }

    t_tiles = now_us();

    if (g_getenv("FOLLOW_WALL_DEBUG") != NULL) {
        g_message("playlist: open covers %.1f ms, tiles %.1f ms, total %.1f ms",
                  (double)(t_covers - t0) / 1000.0,
                  (double)(t_tiles - t_covers) / 1000.0,
                  (double)(t_tiles - t0) / 1000.0);
    }

    P.last_draw_us = now_us();
    P.sel_us       = P.last_draw_us;

    if (config_animations())
        fx_begin(&P.enter_fx, 0.42);
    else
        fx_stop(&P.enter_fx);

    fx_stop(&P.close_fx);

    wallgl_set_active(TRUE);
    ui_animate();
    ui_request_redraw();
    ui_sync_layers();
}

void playlist_close(void)
{
    if (!P.open || P.closing)
        return;

    P.dragging = FALSE;
    P.pan_vx   = P.pan_vy = 0.0;
    P.frame_dirty = TRUE;

    if (config_animations()) {
        P.closing = TRUE;
        fx_begin(&P.close_fx, 0.28);
        ui_animate();
    } else {
        P.open    = FALSE;
        P.closing = FALSE;
        wallgl_set_active(FALSE);
    }

    ui_request_redraw();
    ui_sync_layers();
}

void playlist_toggle(void)
{
    if (P.open)
        playlist_close();
    else
        playlist_open_page();
}

gboolean playlist_is_open(void)
{
    return P.open;
}

gboolean playlist_is_closing(void)
{
    return P.closing;
}

gboolean playlist_animating(void)
{
    if (!P.open || !config_animations())
        return FALSE;

    if (fx_running(&P.enter_fx))
        return TRUE;
    if (P.closing && fx_running(&P.close_fx))
        return TRUE;
    if (hypot(P.pan_vx, P.pan_vy) > 8.0)
        return TRUE;
    if (fabs(P.cam_x - P.cam_tx) > 0.5 || fabs(P.cam_y - P.cam_ty) > 0.5)
        return TRUE;
    if (now_us() - P.sel_us < 360000)
        return TRUE;
    if (P.bg_moving)
        return TRUE;
    if (covercache_animating())
        return TRUE;

    return FALSE;
}

gboolean playlist_chrome_animating(void)
{
    if (!P.open)
        return FALSE;

    if (fx_running(&P.enter_fx))
        return TRUE;
    if (P.closing && fx_running(&P.close_fx))
        return TRUE;

    return FALSE;
}

void playlist_request_frame(void)
{
    if (!P.open)
        return;

    P.frame_dirty = TRUE;

    /* GL 墙由 ui_animate() 的 16ms 泵重建；cairo 回退仍然要靠画布重绘。 */
    if (!wallgl_ready() && app.canvas != NULL)
        gtk_widget_queue_draw(app.canvas);

    ui_request_redraw();
    ui_animate();
}

gboolean playlist_frame_pending(void)
{
    if (!P.open)
        return FALSE;

    if (P.frame_dirty)
        return TRUE;

    return playlist_animating();
}

void playlist_move(gint dx, gint dy)
{
    focus_move(dx, dy);
}

void playlist_page(gint dir)
{
    focus_page(dir);
}

void playlist_home(void)
{
    const APPconfig *cfg = config_get();
    double w, h;
    int cbx, cby;

    if (!P.open || P.closing || cfg->n_tracks == 0)
        return;

    view_size(&w, &h);
    cbx = (int)floor((P.cam_x + w * 0.5) / P.block_w);
    cby = (int)floor((P.cam_y + h * 0.5) / P.block_h);

    P.pan_vx = P.pan_vy = 0.0;
    focus_find_track(0, cbx, cby, 3);
}

void playlist_end(void)
{
    const APPconfig *cfg = config_get();
    double w, h;
    int cbx, cby;

    if (!P.open || P.closing || cfg->n_tracks == 0)
        return;

    view_size(&w, &h);
    cbx = (int)floor((P.cam_x + w * 0.5) / P.block_w);
    cby = (int)floor((P.cam_y + h * 0.5) / P.block_h);

    P.pan_vx = P.pan_vy = 0.0;
    focus_find_track((int)cfg->n_tracks - 1, cbx, cby, 3);
}

static gboolean activate_track(int track)
{
    const APPconfig *cfg = config_get();

    if (!P.open || P.closing || track < 0 ||
        (gsize)track >= cfg->n_tracks)
        return FALSE;

    g_clear_pointer(&P.message, g_free);

    if (!player_play_index(track)) {
        P.message = g_strdup("CANNOT PLAY THIS TRACK");
        ui_request_redraw();
        return FALSE;
    }

    playlist_close();
    return TRUE;
}

gboolean playlist_activate(void)
{
    if (!P.focus_valid)
        return FALSE;

    return activate_track(P.focus_track);
}

gboolean playlist_click(double x, double y)
{
    if (!P.open || P.closing)
        return FALSE;

    if (y < P.header_h || y > P.screen_h - P.bottom_h)
        return TRUE;

    for (int i = P.n_hits - 1; i >= 0; i--) {
        if (x >= P.hit[i].x && x <= P.hit[i].x + P.hit[i].w &&
            y >= P.hit[i].y && y <= P.hit[i].y + P.hit[i].h) {
            activate_track(P.hit[i].track);
            return TRUE;
        }
    }

    return TRUE;
}

void playlist_hover(double x, double y)
{
    gboolean changed = FALSE;

    if (!P.open || P.closing || P.dragging)
        return;

    if (y < P.header_h || y > P.screen_h - P.bottom_h) {
        if (P.hover_valid) {
            P.hover_valid = FALSE;
            changed = TRUE;
        }
    } else {
        gboolean found = FALSE;

        for (int i = P.n_hits - 1; i >= 0; i--) {
            if (x >= P.hit[i].x && x <= P.hit[i].x + P.hit[i].w &&
                y >= P.hit[i].y && y <= P.hit[i].y + P.hit[i].h) {
                if (!P.hover_valid || P.hover_bx != P.hit[i].bx ||
                    P.hover_by != P.hit[i].by ||
                    P.hover_slot != P.hit[i].slot)
                    changed = TRUE;

                P.hover_valid = TRUE;
                P.hover_bx = P.hit[i].bx;
                P.hover_by = P.hit[i].by;
                P.hover_slot = P.hit[i].slot;
                found = TRUE;
                break;
            }
        }

        if (!found && P.hover_valid) {
            P.hover_valid = FALSE;
            changed = TRUE;
        }
    }

    if (changed) {
        GtkWidget *input = (wallgl_ready() && app.chrome != NULL)
            ? app.chrome : app.canvas;

        if (input != NULL)
            gtk_widget_set_cursor_from_name(input,
                                            P.hover_valid ? "pointer" : NULL);

        playlist_request_frame();
    }
}

/* 每帧的相机 / 动效推进。GL 与 cairo 回退共用，返回当前透明度。 */
static double playlist_frame_update(double w, double h)
{
    const APPconfig *cfg = config_get();
    gsize            n_tracks = cfg->n_tracks;
    gint64           now;
    double           dt, enter_p, close_p, vis;

    covercache_sync();
    update_metrics(w, h);
    focus_refresh_world();

    if (app.track != P.last_track) {
        P.last_track  = app.track;
        P.frame_dirty = TRUE;
    }

    if (P.focus_track != P.last_chrome_track) {
        P.last_chrome_track = P.focus_track;

        if (app.chrome != NULL)
            gtk_widget_queue_draw(app.chrome);
    }

    now = now_us();
    dt  = (double)(now - P.last_draw_us) / 1000000.0;
    P.last_draw_us = now;
    dt = CLAMP(dt, 0.0, 0.1);

    if (!P.dragging) {
        if (hypot(P.pan_vx, P.pan_vy) > 8.0) {
            P.cam_x += P.pan_vx * dt;
            P.cam_y += P.pan_vy * dt;

            {
                double decay = exp(-dt * 3.4);

                P.pan_vx *= decay;
                P.pan_vy *= decay;
            }

            if (hypot(P.pan_vx, P.pan_vy) < 8.0)
                P.pan_vx = P.pan_vy = 0.0;

            P.cam_x = CLAMP(P.cam_x, -1e7, 1e7);
            P.cam_y = CLAMP(P.cam_y, -1e7, 1e7);
            P.cam_tx = P.cam_x;
            P.cam_ty = P.cam_y;
        } else if (config_animations()) {
            double k = 1.0 - exp(-dt * 12.0);

            P.cam_x += (P.cam_tx - P.cam_x) * k;
            P.cam_y += (P.cam_ty - P.cam_y) * k;

            if (fabs(P.cam_x - P.cam_tx) < 0.2)
                P.cam_x = P.cam_tx;
            if (fabs(P.cam_y - P.cam_ty) < 0.2)
                P.cam_y = P.cam_ty;
        } else {
            P.cam_x = P.cam_tx;
            P.cam_y = P.cam_ty;
        }
    }

    enter_p = config_animations() ? fx_t(&P.enter_fx) : 1.0;
    close_p = P.closing ? (config_animations() ? fx_t(&P.close_fx) : 1.0) : 0.0;

    if (P.closing && close_p >= 1.0) {

        P.open    = FALSE;
        P.closing = FALSE;
        P.vis     = 0.0;
        fx_stop(&P.close_fx);
        wallgl_set_active(FALSE);
        ui_request_redraw();
        ui_sync_layers();
        return 0.0;
    }

    vis = enter_p * (1.0 - close_p);
    P.vis = vis;

    if (vis <= 0.001)
        return vis;

    /* 背景主色：跟随 focus 封面做 crossfade。
     * focus 的封面还没加载完时保留上一次的颜色，不再临时换成正在播放的画面，
     * 否则把选中的磁贴拖出相机后置色会跳一下 / 看着像失效。 */
    if (n_tracks > 0 && P.focus_track >= 0) {
        const Palette *target = covercache_palette((gsize)P.focus_track);

        if (target != NULL && target->has) {
            if (!P.bg_has) {
                P.bg_pal = *target;
                P.bg_has = TRUE;
            } else if (config_animations()) {
                Palette next = palette_mix(&P.bg_pal, target, 0.10);
                double  dr = next.seed_r - P.bg_pal.seed_r;
                double  dg = next.seed_g - P.bg_pal.seed_g;
                double  db = next.seed_b - P.bg_pal.seed_b;

                P.bg_pal = next;
                P.bg_moving = (fabs(dr) + fabs(dg) + fabs(db)) > 0.002;
            } else {
                P.bg_pal = *target;
                P.bg_moving = FALSE;
            }
        }
    } else {
        P.bg_moving = FALSE;
    }

    return vis;
}

/* GL 层：构建可见磁贴列表并推给 wallgl。 */
void playlist_build_frame(double w, double h)
{
    const APPconfig *cfg = config_get();
    WallGlFrame      f = { 0 };
    WallGlTile       selected_tile = { 0 };
    const Palette   *focus_pal;
    int              n = (int)cfg->n_tracks;
    double           vis;
    gboolean         have_selected = FALSE;

    if (!P.open)
        return;

    vis = playlist_frame_update(w, h);

    if (!P.open)
        return;

    f.view_w     = w;
    f.view_h     = h;
    f.dim_top    = P.header_h;
    f.dim_bottom = h - P.bottom_h;
    f.vis        = vis;
    f.offset_y   = (1.0 - vis) * 28.0;
    f.selected   = -1;

    background_color(&f.bg_r, &f.bg_g, &f.bg_b);

    {
        const Palette *pal = background_palette();

        if (pal != NULL) {
            f.tint         = TRUE;
            f.tint_seed_r  = pal->seed_r;
            f.tint_seed_g  = pal->seed_g;
            f.tint_seed_b  = pal->seed_b;
            f.tint_alt_r   = pal->alt_r;
            f.tint_alt_g   = pal->alt_g;
            f.tint_alt_b   = pal->alt_b;
        }
    }

    f.tiles = g_frame_tiles;

    P.n_hits = 0;

    if (n > 0 && vis > 0.001) {
        double overscan = P.cell * 0.8;
        int    bx0 = (int)floor((P.cam_x - overscan) / P.block_w);
        int    bx1 = (int)floor((P.cam_x + w + overscan) / P.block_w);
        int    by0 = (int)floor((P.cam_y - overscan) / P.block_h);
        int    by1 = (int)floor((P.cam_y + h + overscan) / P.block_h);

        for (int by = by0; by <= by1; by++) {
            for (int bx = bx0; bx <= bx1; bx++) {
                guint32 bh = block_hash(bx, by);
                const PlTemplate *tpl = block_template(bh);
                int flip = block_flip(bh);
                int order[PL_TEMPLATE_MAX];

                block_order(bh, n, tpl->count, order);

                for (int s = 0; s < tpl->count; s++) {
                    double   wx, wy, ww, wh, x, y;
                    gboolean selected, hovered;
                    WallGlTile t;

                    slot_rect(bx, by, tpl, flip, s, &wx, &wy, &ww, &wh);
                    x = wx - P.cam_x;
                    y = wy - P.cam_y;

                    if (x + ww < -overscan || x > w + overscan ||
                        y + wh < -overscan || y > h + overscan)
                        continue;

                    selected = P.focus_valid && bx == P.focus_bx &&
                               by == P.focus_by && s == P.focus_slot;
                    hovered  = P.hover_valid && bx == P.hover_bx &&
                               by == P.hover_by && s == P.hover_slot;

                    if (P.n_hits < PL_MAX_HITS) {
                        PlHit *hit = &P.hit[P.n_hits++];

                        hit->track = order[s];
                        hit->bx = bx;
                        hit->by = by;
                        hit->slot = s;
                        hit->x = x;
                        hit->y = y;
                        hit->w = ww;
                        hit->h = wh;
                    }

                    t.track    = (gsize)order[s];
                    t.x        = x;
                    t.y        = y;
                    t.w        = ww;
                    t.h        = wh;
                    t.selected = selected;
                    t.hovered  = hovered;
                    t.current  = t.track == (gsize)app.track;
                    t.reveal   = (float)covercache_reveal(t.track);

                    if (selected) {
                        selected_tile = t;
                        have_selected = TRUE;
                    } else if (f.n_tiles < PL_MAX_HITS) {
                        f.tiles[f.n_tiles++] = t;
                    }
                }
            }
        }
    }

    if (have_selected) {
        double pop = 1.0;

        if (config_animations()) {
            double age = (double)(now_us() - P.sel_us) / 1000000.0;

            pop = 1.0 + 0.028 * exp(-age * 12.0);
        }

        f.selected = f.n_tiles;
        f.selected_pop = pop;
        f.tiles[f.n_tiles++] = selected_tile;

        focus_pal = covercache_palette(selected_tile.track);

        if (focus_pal != NULL && focus_pal->has) {
            f.glow     = TRUE;
            f.glow_r   = focus_pal->seed_r;
            f.glow_g   = focus_pal->seed_g;
            f.glow_b   = focus_pal->seed_b;
        }
    }

    wallgl_set_frame(&f);
    P.frame_dirty = FALSE;
    P.gl_chrome   = TRUE;
}

void playlist_draw_chrome(cairo_t *cr, double w, double h)
{
    const APPconfig *cfg = config_get();
    gsize            n_tracks = cfg->n_tracks;
    double           vis = P.vis;

    if (!P.open || !P.gl_chrome || vis <= 0.001)
        return;

    if (vis < 0.999) {
        cairo_push_group(cr);
        cairo_translate(cr, 0.0, (1.0 - vis) * 28.0);
    }

    if (n_tracks == 0)
        draw_empty(cr, w, h);

    draw_header(cr, w, h, n_tracks);
    draw_bottom(cr, w, h, n_tracks);

    if (vis < 0.999) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, vis);
    }
}

void playlist_draw(cairo_t *cr, double w, double h)
{
    const APPconfig *cfg = config_get();
    gsize            n_tracks = cfg->n_tracks;
    double           vis;

    if (!P.open)
        return;

    P.gl_chrome = FALSE;
    vis = playlist_frame_update(w, h);

    if (vis <= 0.001)
        return;

    if (vis < 0.999) {
        cairo_push_group(cr);
        cairo_translate(cr, 0.0, (1.0 - vis) * 28.0);
    }

    draw_background(cr, w, h);

    if (n_tracks == 0) {
        draw_empty(cr, w, h);
    } else {
        draw_wall(cr, w, h);
    }

    draw_header(cr, w, h, n_tracks);
    draw_bottom(cr, w, h, n_tracks);

    if (vis < 0.999) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, vis);
    }
}

void playlist_clear(void)
{
    tile_jobs_clear();
    wallgl_clear();
    g_clear_pointer(&P.message, g_free);
    tile_cache_clear();
    covercache_clear();
    memset(&P, 0, sizeof(P));
}
