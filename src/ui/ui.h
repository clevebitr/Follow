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

/* 一个二维码槽：内容 + 胶囊文字 +（可选）平台图标，以及按像素缓存好的 surface */
typedef struct {
    gchar           *text;
    gchar           *label;
    Platform         platform;
    gboolean         with_icon;
    QRcode          *code;
    cairo_surface_t *surface;
    int              pixels;
} QrSlot;

typedef struct {
    /* GTK */
    GtkWidget *window;
    GtkWidget *header_bar;   /* 无 CSD 外框时放在内容顶部 */
    GtkWidget *header_title;
    GtkWidget *canvas;
    GtkWidget *lyricgl;     /* 歌词 GPU 层（透明，盖在主画布上） */
    GtkWidget *wallgl;      /* 歌曲墙 GPU 层 */
    GtkWidget *chrome;      /* 歌曲墙表头 / 底栏 / 帮助页（透明层） */
    GtkWidget *perf;        /* 性能检测层（F12） */
    GtkWidget *bg_picture;
    GtkWidget *bg_gl;       /* 动态背景 GPU 层（失败时回退到 bg_picture） */
    guint      tick_id;
    guint      bg_tick_id;

    /* 画面刷新 */
    gboolean dirty;
    gboolean win_dragging;   /* 窗口拖动 / 缩放中：暂停动画 */
    gboolean chrome_dirty;   /* 歌曲墙表头 / 底栏需要重画 */
    gboolean help;
    gboolean last_playing;
    gboolean last_paused;
    gint     last_width;
    gint     last_height;
    gint     shown_second;       /* 播放位置的整秒 */
    gint64   shown_wall_second;  /* 墙钟的整秒 */

    /* 播放 */
    Media    media;
    gint     track;               /* -1 = 还没有选曲目 */
    gint     queue_next;          /* NEXT 预告指定曲目；-1 = 跟随机序 */
    gint     repeat;              /* RepeatMode */
    gboolean shuffle;
    gint     history[64];         /* 随机播放历史 */
    gint     history_len;
    guint    resolve_gen;         /* 丢弃过期的网络解析结果 */
    gboolean source_pending;      /* 远程解析进行中 */
    gboolean source_cached;       /* 本次播放用的是解析缓存直链 */
    gboolean source_retried;      /* 缓存直链失败后已重解析过一次 */
    gchar   *resolved_title;      /* 网络来源解析出的曲名 */
    gchar   *resolved_artist;
    gchar   *source_error;        /* 网络解析失败的原因（显示在歌词那行） */

    /* 歌词 */
    Lyrics  *lyrics;
    gchar   *lyric_line;          /* 当前要显示的一行 */
    gchar   *lyric_next;          /* 下一行（预显示：淡色、不发光） */
    gssize   lyric_index;         /* 当前行号（-1 = 还没到第一行） */
    gchar   *lyric_prev;          /* 刚翻过去的一行：过渡时上浮淡出 */
    Fx       fx_lyric;            /* 换行过渡（淡入淡出 + 上浮） */
    gchar   *lyric_error;         /* 找不到 / 解析失败的原因 */
    gchar   *lyrics_spec;         /* 当前歌词源 */
    guint    lyrics_gen;
    gboolean lyrics_pending;
    gboolean lyrics_fit_checked;
    gint     lrc_offset_ms;
    gint     lrc_offset_base;     /* ini 里的初始值，\ 键重置 */

    /* 封面 */
    cairo_surface_t *cover;       /* 原图，长边 <= 768 */
    cairo_surface_t *cover_scaled;
    int              cover_scaled_px;
    guint            cover_gen;   /* 丢弃过期的封面下载 */
    Palette          palette;     /* 从封面里取的种子色 */

    /* 动态背景：GtkPicture，
     * 由 GTK 的 GL 渲染器负责放大 —— CPU 每帧只生成这么点像素。 */
    cairo_surface_t *bg_base;     /* 模糊封面缩略图*/
    cairo_surface_t *bg_prev;     /* 上一张 */
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

static inline void ui_request_redraw(void)
{
    app.dirty = TRUE;
}

void ui_animate(void);

/* 根据「歌曲墙是否打开 / GL 是否可用」切换各图层的可见性，
 * 避免在墙全屏遮住画面时还去合成背景和主画布。 */
void ui_sync_layers(void);

/* 命令面板用：重新读取 src/config.ini / 切换全屏。 */
void ui_reload_config(void);
void ui_toggle_fullscreen(void);

#endif /* FOLLOW_UI_H */
