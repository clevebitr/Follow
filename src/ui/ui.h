#ifndef FOLLOW_UI_H
#define FOLLOW_UI_H

#include <gtk/gtk.h>
#include <qrencode.h>

#include "config.h"
#include "fx.h"
#include "lyrics.h"
#include "media.h"
#include "palette.h"
#include "platform.h"
#include "stats.h"

/* 全局应用状态：整个程序只有一份（app）。
 *
 * 分工：
 *   main.c     生命周期 + 键盘 + 定时器（唯一的"胶水"）
 *   player.c   播放控制、网络解析、曲目信息
 *   lyricview  歌词加载 / 在线源 / 偏移 / 当前行
 *   cover.c    封面加载（本地 / 网络缓存）与绘制
 *   backdrop   动态背景（模糊封面 + 漂移的种子色）
 *   qrcard     二维码槽 + 平台胶囊
 *   layout     一次绘制用到的所有几何量
 *   stage      主画面（大时钟 / 备注 / 歌词 / 右列 / 统计）
 *   help       F1 快捷键页
 *
 * 所有内存都挂在 app 上，退出时由各自的 *_clear() 收回。
 */

/* 一个二维码槽：内容 + 胶囊文字 +（可选）平台图标，以及按像素缓存好的 surface */
typedef struct {
    gchar           *text;
    gchar           *label;
    Platform         platform;
    gboolean         with_icon;
    QRcode          *code;
    cairo_surface_t *surface;
    int              pixels;      /* 缓存 surface 的边长（两个二维码强制同一尺寸） */
} QrSlot;

typedef struct {
    /* GTK */
    GtkWidget *window;
    GtkWidget *canvas;
    GtkWidget *bg_picture;
    guint      tick_id;
    guint      bg_tick_id;

    /* 画面刷新：只在"看得见的东西变了"时置脏 */
    gboolean dirty;
    gboolean help;
    gboolean last_playing;
    gboolean last_paused;
    gint     last_width;
    gint     last_height;
    gint     shown_second;       /* 播放位置的整秒（变了才重画） */
    gint64   shown_wall_second;  /* 墙钟的整秒（暂停时也要每秒重画一次） */

    /* 播放 */
    Media    media;
    gint     track;               /* -1 = 还没有选曲目 */
    gint     repeat;              /* RepeatMode */
    gboolean shuffle;
    gint     history[64];         /* 随机播放历史，用来支持"上一首" */
    gint     history_len;
    guint    resolve_gen;         /* 丢弃过期的网络解析结果 */
    gchar   *resolved_title;      /* 网络来源解析出的曲名（ini 缺字段时补位） */
    gchar   *resolved_artist;

    /* 歌词 */
    Lyrics  *lyrics;
    gchar   *lyric_line;          /* 当前要显示的一行 */
    gchar   *lyric_error;         /* 找不到 / 解析失败的原因 */
    gchar   *lyrics_spec;         /* 当前歌词源（日志用） */
    guint    lyrics_gen;
    gboolean lyrics_pending;
    gboolean lyrics_fit_checked;  /* 时长诊断只做一次 */
    gint     lrc_offset_ms;       /* 运行时可用 [ ] 微调 */
    gint     lrc_offset_base;     /* ini 里的初始值，\ 键重置用 */

    /* 封面 */
    cairo_surface_t *cover;       /* 原图，长边 <= 768 */
    cairo_surface_t *cover_scaled;
    int              cover_scaled_px;
    guint            cover_gen;   /* 丢弃过期的封面下载 */
    Palette          palette;     /* 从封面里取的种子色 */

    /* 动态背景：一张很小（≈256px 宽）的纹理交给 GtkPicture，
     * 由 GTK 的 GL 渲染器负责放大 —— CPU 每帧只生成这么点像素。 */
    cairo_surface_t *bg_base;     /* 模糊封面缩略图（换封面 / 改宽高比时重建） */
    cairo_surface_t *bg_prev;     /* 上一张，退潮过渡时从左边滑出去 */
    Palette          pal_bg_prev; /* 上一张的种子色，过渡期间和新色插值 */
    cairo_surface_t *bg_frame;    /* 每帧复用的画布 */
    int              bg_base_w;
    int              bg_base_h;
    double           bg_phase;    /* 动画相位（秒） */

    /* 二维码 */
    QrSlot qr[2];                 /* 0 = 曲目页面，1 = 自定义链接 */

    /* 过渡：背景退潮（backdrop）+ 组件撤桌重上（stage） */
    Fx     fx_bg;
    Fx     fx_stage;
    guint  anim_id;               /* 过渡期间的 16ms 画布帧循环 */
    cairo_surface_t *snapshot;    /* 上一首歌的整屏快照，做退场用 */

    /* 收听统计（左下角） */
    Stats  stats;
    gint64 stats_last_us;
    gint64 stats_saved_us;
    double stats_fraction;       /* 不足 1 秒的零头，攒够再记 */
    gint   stats_dirty_seconds;  /* 有没落盘的秒数 */
} App;

extern App app;

/* 标记"该重画了"；定时器里每 100ms 统一提交一次 */
static inline void ui_request_redraw(void)
{
    app.dirty = TRUE;
}

/* 开始 / 续上过渡帧循环（16ms 一帧，只在过渡期间存在） */
void ui_animate(void);

#endif /* FOLLOW_UI_H */
