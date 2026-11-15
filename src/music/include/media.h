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

#ifndef MEDIA_H
#define MEDIA_H

#include <glib.h>

#include "miniaudio.h"

#define MEDIA_CHANNELS        2
#define MEDIA_FALLBACK_RATE   48000
#define MEDIA_BUFFER_SECONDS  3
#define MEDIA_SCOPE_SAMPLES   2048

typedef enum {
    MEDIA_STOPPED = 0,
    MEDIA_PLAYING,
    MEDIA_PAUSED,
    MEDIA_LOADING
} MediaState;

typedef struct _Media Media;

struct _Media {
    ma_device  device;
    ma_context context;
    int        context_ready;
    int        device_ok;
    ma_uint32  sample_rate;
    float      volume;
    int        muted;
    char       output_name[256];

    float     *ring;
    ma_uint32  ring_frames;
    ma_uint64  ring_write;
    ma_uint64  ring_read;

    /* 可视化取样环：单声道 SPSC。 */
    float     *scope;
    ma_uint64  scope_write;

    int        state;
    int        finished;
    int        eof;
    int        has_stream;
    ma_uint64  cursor;
    ma_uint64  length;

    GThread   *thread;
    int        thread_stop;
    int        cmd_load;
    ma_int64   cmd_seek;
    GMutex     cmd_lock;
    gchar     *cmd_url;
    gchar     *cmd_headers;
    int        cmd_ranged;

    gchar      error[256];

    int        ready;
};

int  media_init(Media *m);
void media_clear(Media *m);

/* 开始播放；ranged = TRUE 时使用 httpstream 分块读取。 */
int  media_play(Media *m, const char *url, const char *headers, gboolean ranged);

int  media_pause(Media *m);
int  media_continue(Media *m);
void media_stop(Media *m);

double media_position(Media *m);
double media_duration(Media *m);
int    media_seek(Media *m, double seconds);

void   media_set_volume(Media *m, double volume);
double media_volume(Media *m);
void   media_set_muted(Media *m, gboolean muted);
gboolean media_muted(Media *m);

int    media_output_devices(Media *m, GPtrArray *names);

/* 切换输出设备；成功返回 0。 */
int    media_set_output_device(Media *m, const char *name);
const char *media_output_device_name(Media *m);

int  media_is_playing(Media *m);
int  media_is_paused(Media *m);
int  media_is_loading(Media *m);
int  media_is_finished(Media *m);

/* 消费 finished 标志，避免同一首歌连跳两次。 */
void media_ack_finished(Media *m);

/* 切歌时清除上一首的错误与 finished。 */
void media_begin_track(Media *m);

const char *media_error(Media *m);

/* 读取可视化样本；不加锁，返回实际样本数。 */
int  media_read_scope(Media *m, float *dst, int n);

#endif
