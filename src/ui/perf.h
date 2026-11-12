/**
 * created: 2026/11/12
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/12
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */
#ifndef FOLLOW_PERF_H
#define FOLLOW_PERF_H

#include <gtk/gtk.h>

/* 性能检测层（F12）：叠在画面左上角的一小块面板。
 *
 * 数据来源分两类：
 *   1. 帧率 / 帧间隔：窗口 GdkFrameClock 的 after-paint。这是「真正合成了一帧」
 *      的时刻，和 uidbg 里按主画布重画次数估算的口径不同（歌词走独立的
 *      GLArea，主画布不重画时画面其实还在动）。
 *   2. 各层 CPU 耗时：绘制函数里成对调用 perf_phase_begin/end。
 *
 * 面板每 200ms 刷新一次；没开时只做很便宜的帧时间采样（一次 mono 时间 +
 * 一个信号跳转），埋点全是空调用。
 *
 *   F12                        开关（可在 config.ini [keys] perf 改）
 *   FOLLOW_PERF=1              启动时就打开
 */

typedef enum {
    PERF_PHASE_STAGE = 0,    /* 主画布 cairo（stage_render） */
    PERF_PHASE_CHROME,       /* 墙的表头 / 帮助页 cairo */
    PERF_PHASE_LYRIC,        /* 歌词 GL（lyricgl render） */
    PERF_PHASE_WALL,         /* 歌曲墙 GL（wallgl render） */
    PERF_PHASE_BACKDROP_GL,  /* 动态背景 GL（backdropgl render） */
    PERF_PHASE_BACKDROP_CPU, /* 动态背景 CPU 回退（backdrop_tick） */
    PERF_PHASE_COUNT
} PerfPhase;

/* 建面板并接上窗口的帧时钟。main.c 在 overlay 里加这一层。 */
GtkWidget *perf_new(GtkWidget *window);

/* 释放帧时钟引用（退出时调一次）。 */
void perf_clear(void);

void     perf_toggle(void);
gboolean perf_visible(void);

/* 绘制耗时埋点：成对调用同一 phase。 */
void perf_phase_begin(PerfPhase phase);
void perf_phase_end(PerfPhase phase);

/* 纹理上传字节数（MB/s 显示用）。 */
void perf_note_upload(gsize bytes);

#endif /* FOLLOW_PERF_H */
