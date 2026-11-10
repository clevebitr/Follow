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

#ifndef MEDIA_H
#define MEDIA_H

#include "miniaudio.h"

/* 播放层：miniaudio 输出设备 + ffmpeg 解码。
 *
 * 线程模型：
 *   解码线程  —— ffmpeg 打开/读取/解码/重采样，把 f32 立体声推进环形缓冲。
 *                网络 I/O 全在这里，绝不碰音频线程。
 *   音频线程  —— 只从环形缓冲里搬样本，没有任何锁、没有 I/O。
 *   UI 线程   —— 只改原子状态（播放/暂停/seek），位置和时长都是原子读。
 *
 * 环形缓冲是 SPSC，读写各用原子游标；容量 MEDIA_BUFFER_SECONDS 秒，够吸收网络抖动。
 */

#define MEDIA_CHANNELS        2
#define MEDIA_FALLBACK_RATE   48000
#define MEDIA_BUFFER_SECONDS  3      /* 环形缓冲长度（秒） */

typedef enum {
    MEDIA_STOPPED = 0,
    MEDIA_PLAYING,
    MEDIA_PAUSED,
    MEDIA_LOADING      /* 正在打开流（网络解析 + 探测） */
} MediaState;

typedef struct _Media Media;

struct _Media {
    ma_device  device;
    ma_uint32  sample_rate;

    /* 输出环形缓冲（f32 交织立体声） */
    float     *ring;
    ma_uint32  ring_frames;     /* 容量（帧） */
    ma_uint64  ring_write;      /* 原子：写入游标（单调递增） */
    ma_uint64  ring_read;       /* 原子：读出游标（单调递增） */

    /* 状态（原子读写） */
    int        state;
    int        finished;
    int        eof;
    int        has_stream;
    ma_uint64  cursor;          /* 已播放帧数 */
    ma_uint64  length;          /* 总帧数，0 = 未知（直播流） */

    /* 解码线程 */
    GThread   *thread;
    int        thread_stop;     /* 原子 */
    int        cmd_load;        /* 原子：1 = 有新的 URL 要打开 */
    ma_int64   cmd_seek;        /* 原子：>=0 = 要 seek 到的帧号，<0 = 没有 */
    GMutex     cmd_lock;
    gchar     *cmd_url;
    gchar     *cmd_headers;
    int        cmd_ranged;      /* 原子：1 = 用定长 Range 分块读（YouTube） */

    gchar      error[256];

    int        ready;
};

/* 初始化输出设备 + 启动解码线程 */
int  media_init(Media *m);
void media_clear(Media *m);

/* 开始播放：url 可以是本地路径或 http(s)，headers 可为 NULL。
 * ranged = TRUE 时不走 FFmpeg 的 http，改用 httpstream（定长 Range + libcurl）：
 * googlevideo 会拒绝 FFmpeg 那种开放式 Range: bytes=0-，只能自己分块取。 */
int  media_play(Media *m, const char *url, const char *headers, gboolean ranged);

int  media_pause(Media *m);
int  media_continue(Media *m);
void media_stop(Media *m);

double media_position(Media *m);
double media_duration(Media *m);
int    media_seek(Media *m, double seconds);

int  media_is_playing(Media *m);
int  media_is_paused(Media *m);
int  media_is_loading(Media *m);
int  media_is_finished(Media *m);

/* 消费掉标志：换歌是异步的（网络曲目要等解析），
 * 不消费的话下一次 tick 还会看到同一个 finished，导致连跳两首。 */
void media_ack_finished(Media *m);

/* 切歌时调用：清掉上一首的错误提示 + 消费掉 finished。
 * （媒体真正开始加载要等网络解析回来，那之前界面不该挂着上一首的错误） */
void media_begin_track(Media *m);

/* 最近一次加载失败的原因；没有错误返回 NULL */
const char *media_error(Media *m);

#endif /* MEDIA_H */
