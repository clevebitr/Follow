#define MINIAUDIO_IMPLEMENTATION
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include "media.h"

#define FOLLOW_UA \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"

/* 一次解码最多产出多少帧：aac 1024、mp3 1152、opus 960，留大一点 */
#define MEDIA_DECODE_CHUNK 8192

/* ---------- ffmpeg 上下文 ---------- */

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext  *dec;
    SwrContext      *swr;
    AVPacket        *pkt;
    AVFrame         *frame;
    int              stream;
    int              out_rate;
    int              out_channels;
    int              eof;
    ma_uint64        length_frames;

    float           *scratch;      /* 重采样中转 */
    int              scratch_cap;  /* 帧数 */
} FFMedia;

static char *dup_headers_or_null(const char *headers)
{
    return (headers != NULL && *headers != '\0') ? g_strdup(headers) : NULL;
}

/* ---------- 环形缓冲 ---------- */

static ma_uint64 ring_free_frames(Media *m)
{
    ma_uint64 w = __atomic_load_n(&m->ring_write, __ATOMIC_ACQUIRE);
    ma_uint64 r = __atomic_load_n(&m->ring_read, __ATOMIC_ACQUIRE);

    if (w < r)                       /* seek 时可能短暂倒挂 */
        return 0;

    return (ma_uint64)m->ring_frames - (w - r);
}

static void ring_reset(Media *m)
{
    __atomic_store_n(&m->ring_write, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&m->ring_read, 0, __ATOMIC_RELEASE);
}

/* 只有解码线程调用 */
static void ring_push(Media *m, const float *src, ma_uint32 frames)
{
    ma_uint64 w = m->ring_write;
    ma_uint64 free_frames = ring_free_frames(m);
    ma_uint32 done = 0;

    if ((ma_uint64)frames > free_frames)      /* 兜底：宁可丢一点也别越界 */
        frames = (ma_uint32)free_frames;

    while (done < frames) {
        ma_uint32 idx = (ma_uint32)(w % m->ring_frames);
        ma_uint32 chunk = MIN(frames - done, m->ring_frames - idx);

        memcpy(&m->ring[(gsize)idx * MEDIA_CHANNELS],
               &src[(gsize)done * MEDIA_CHANNELS],
               (gsize)chunk * MEDIA_CHANNELS * sizeof(float));

        w += chunk;
        done += chunk;
    }

    __atomic_store_n(&m->ring_write, w, __ATOMIC_RELEASE);
}

/* ---------- 音频回调：只搬样本 ---------- */

static void media_data_callback(ma_device *device, void *output, const void *input,
                                ma_uint32 frame_count)
{
    Media    *m = (Media *)device->pUserData;
    float    *out = (float *)output;
    ma_uint64 r, w, avail;
    ma_uint32 take;

    (void)input;

    memset(out, 0, (size_t)frame_count * MEDIA_CHANNELS * sizeof(float));

    if (m == NULL)
        return;

    if (__atomic_load_n(&m->state, __ATOMIC_ACQUIRE) != MEDIA_PLAYING)
        return;

    r     = __atomic_load_n(&m->ring_read, __ATOMIC_ACQUIRE);
    w     = __atomic_load_n(&m->ring_write, __ATOMIC_ACQUIRE);
    avail = (w > r) ? (w - r) : 0;
    take  = (ma_uint32)MIN((ma_uint64)frame_count, avail);

    {
        ma_uint32 done = 0;

        while (done < take) {
            ma_uint32 idx = (ma_uint32)((r + done) % m->ring_frames);
            ma_uint32 chunk = MIN(take - done, m->ring_frames - idx);

            memcpy(&out[(gsize)done * MEDIA_CHANNELS],
                   &m->ring[(gsize)idx * MEDIA_CHANNELS],
                   (gsize)chunk * MEDIA_CHANNELS * sizeof(float));

            done += chunk;
        }
    }

    __atomic_store_n(&m->ring_read, r + take, __ATOMIC_RELEASE);
    __atomic_fetch_add(&m->cursor, take, __ATOMIC_RELEASE);

    /* 缓冲抽干了而且解码线程已经到了文件尾 → 这首放完了 */
    if (take == 0 && __atomic_load_n(&m->eof, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&m->has_stream, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&m->finished, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&m->state, MEDIA_STOPPED, __ATOMIC_RELEASE);
    }
}

/* ---------- ffmpeg ---------- */

static void ff_close(FFMedia *ff)
{
    if (ff->swr != NULL) swr_free(&ff->swr);
    if (ff->frame != NULL) av_frame_free(&ff->frame);
    if (ff->pkt != NULL) av_packet_free(&ff->pkt);
    if (ff->dec != NULL) avcodec_free_context(&ff->dec);
    if (ff->fmt != NULL) avformat_close_input(&ff->fmt);

    g_free(ff->scratch);
    ff->scratch = NULL;
    ff->scratch_cap = 0;
    ff->stream = -1;
    ff->length_frames = 0;
}

static void set_error(Media *m, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void set_error(Media *m, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    g_vsnprintf(m->error, sizeof(m->error), fmt, ap);
    va_end(ap);
}

static int ff_open(FFMedia *ff, Media *m, const char *url, const char *headers)
{
    const AVCodec *codec = NULL;
    AVDictionary  *opts = NULL;
    int            result;

    av_dict_set(&opts, "user_agent", FOLLOW_UA, 0);
    av_dict_set(&opts, "rw_timeout", "15000000", 0);      /* 15s 读写超时 */
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);

    if (headers != NULL && *headers != '\0')
        av_dict_set(&opts, "headers", headers, 0);

    result = avformat_open_input(&ff->fmt, url, NULL, &opts);
    av_dict_free(&opts);

    if (result < 0) {
        char buf[128] = {0};

        av_strerror(result, buf, sizeof(buf));
        set_error(m, "打不开：%s", buf);
        ff_close(ff);
        return -1;
    }

    if (avformat_find_stream_info(ff->fmt, NULL) < 0) {
        set_error(m, "读不到流信息");
        ff_close(ff);
        return -1;
    }

    ff->stream = av_find_best_stream(ff->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (ff->stream < 0 || codec == NULL) {
        set_error(m, "文件里没有音频流");
        ff_close(ff);
        return -1;
    }

    ff->dec = avcodec_alloc_context3(codec);
    if (ff->dec == NULL) {
        set_error(m, "分配解码器失败");
        ff_close(ff);
        return -1;
    }

    if (avcodec_parameters_to_context(ff->dec, ff->fmt->streams[ff->stream]->codecpar) < 0 ||
        avcodec_open2(ff->dec, codec, NULL) < 0) {
        set_error(m, "打不开解码器（%s）", codec->name);
        ff_close(ff);
        return -1;
    }

    ff->pkt   = av_packet_alloc();
    ff->frame = av_frame_alloc();
    if (ff->pkt == NULL || ff->frame == NULL) {
        set_error(m, "分配 packet/frame 失败");
        ff_close(ff);
        return -1;
    }

    ff->out_rate     = (int)m->sample_rate;
    ff->out_channels = MEDIA_CHANNELS;

    /* 时长（直播流可能没有） */
    {
        AVStream *st = ff->fmt->streams[ff->stream];
        gint64    us = 0;

        if (st->duration > 0)
            us = av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
        else if (ff->fmt->duration > 0)
            us = ff->fmt->duration;

        if (us > 0)
            ff->length_frames = (ma_uint64)((double)us * (double)ff->out_rate / 1000000.0);
    }

    g_message("media: opened %s — %s, %d Hz, %d ch, %.1fs",
              url, codec->name, ff->dec->sample_rate, ff->dec->ch_layout.nb_channels,
              (double)ff->length_frames / (double)ff->out_rate);

    return 0;
}

/* 保证 swr 跟当前帧匹配 */
static int ff_prepare_swr(FFMedia *ff, const AVFrame *frame)
{
    AVChannelLayout in_layout;

    if (ff->swr != NULL)
        return 0;

    if (frame->ch_layout.nb_channels > 0)
        av_channel_layout_copy(&in_layout, &frame->ch_layout);
    else
        av_channel_layout_default(&in_layout, 2);

    {
        AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
        int            result;

        result = swr_alloc_set_opts2(&ff->swr,
                                    &out_layout, AV_SAMPLE_FMT_FLT, ff->out_rate,
                                    &in_layout, (enum AVSampleFormat)frame->format,
                                    frame->sample_rate,
                                    0, NULL);

        av_channel_layout_uninit(&in_layout);

        if (result < 0 || ff->swr == NULL)
            return -1;

        if (swr_init(ff->swr) < 0) {
            swr_free(&ff->swr);
            return -1;
        }
    }

    return 0;
}

static void ff_ensure_scratch(FFMedia *ff, int frames)
{
    if (frames <= ff->scratch_cap)
        return;

    ff->scratch_cap = frames;
    ff->scratch = g_renew(float, ff->scratch, (gsize)frames * MEDIA_CHANNELS);
}

/* 把当前 frame 重采样成 f32 立体声并推进环形缓冲 */
static int ff_convert(FFMedia *ff, Media *m)
{
    int out_max;

    if (ff_prepare_swr(ff, ff->frame) != 0)
        return -1;

    out_max = (int)av_rescale_rnd(
        swr_get_delay(ff->swr, ff->frame->sample_rate) + ff->frame->nb_samples,
        ff->out_rate, ff->frame->sample_rate, AV_ROUND_UP);

    ff_ensure_scratch(ff, out_max);

    {
        uint8_t *dst[1] = { (uint8_t *)ff->scratch };
        int      produced = swr_convert(ff->swr, dst, out_max,
                                        (const uint8_t **)ff->frame->extended_data,
                                        ff->frame->nb_samples);

        if (produced > 0)
            ring_push(m, ff->scratch, (ma_uint32)produced);
    }

    return 0;
}

/* 解一包，把 PCM 推进环形缓冲。返回 >0 = 这一轮有产出；-1 = 文件结束 */
static int ff_decode_some(FFMedia *ff, Media *m)
{
    for (;;) {
        int read_result = av_read_frame(ff->fmt, ff->pkt);

        if (read_result == AVERROR_EOF) {
            /* 冲刷解码器：送 NULL 进去，把残留帧取完 */
            avcodec_send_packet(ff->dec, NULL);

            for (;;) {
                int got = avcodec_receive_frame(ff->dec, ff->frame);

                if (got < 0)
                    return -1;

                ff_convert(ff, m);
                av_frame_unref(ff->frame);
            }
        }

        if (read_result < 0)
            return -1;

        if (ff->pkt->stream_index != ff->stream) {
            av_packet_unref(ff->pkt);
            continue;
        }

        if (avcodec_send_packet(ff->dec, ff->pkt) < 0) {
            av_packet_unref(ff->pkt);
            continue;
        }

        av_packet_unref(ff->pkt);

        for (;;) {
            int got = avcodec_receive_frame(ff->dec, ff->frame);

            if (got == AVERROR(EAGAIN) || got == AVERROR_EOF)
                break;
            if (got < 0)
                return -1;

            if (ff_convert(ff, m) != 0) {
                av_frame_unref(ff->frame);
                return -1;
            }

            av_frame_unref(ff->frame);
        }

        return 1;   /* 让外层回去检查暂停 / seek / 退出 */
    }
}

/* 返回 FALSE 表示这个流不支持随机访问（有些 CDN 不接受 Range 请求），
 * 调用方必须保持原来的播放位置，否则会被误当成"放完了"。 */
static gboolean ff_seek(FFMedia *ff, ma_uint64 frame, Media *m)
{
    gint64 us = (gint64)((double)frame * 1000000.0 / (double)m->sample_rate);
    /* stream_index = -1 时时间戳单位是 AV_TIME_BASE（微秒）；
     * 传具体流索引的话得先换算到该流的 time_base —— 之前混用导致 seek 直接跳到文件末尾。 */
    int    result = avformat_seek_file(ff->fmt, -1, INT64_MIN, us, INT64_MAX,
                                       AVSEEK_FLAG_BACKWARD);

    if (result < 0) {
        char buf[96] = {0};

        av_strerror(result, buf, sizeof(buf));
        g_warning("media: seek not supported here (%s)", buf);
        return FALSE;
    }

    avcodec_flush_buffers(ff->dec);

    if (ff->swr != NULL)
        swr_init(ff->swr);

    if (ff->frame != NULL)
        av_frame_unref(ff->frame);

    __atomic_store_n(&ff->eof, 0, __ATOMIC_RELEASE);

    return TRUE;
}

/* ---------- 解码线程 ---------- */

static gpointer media_decode_thread(gpointer data)
{
    Media   *m = (Media *)data;
    FFMedia *ff = g_new0(FFMedia, 1);

    ff->stream = -1;

    for (;;) {
        ma_int64 seek;

        if (__atomic_load_n(&m->thread_stop, __ATOMIC_ACQUIRE))
            break;

        /* 1) 打开新地址 */
        if (__atomic_exchange_n(&m->cmd_load, 0, __ATOMIC_ACQ_REL)) {
            gchar *url = NULL;
            gchar *headers = NULL;

            g_mutex_lock(&m->cmd_lock);
            url     = m->cmd_url;
            headers = m->cmd_headers;
            m->cmd_url     = NULL;
            m->cmd_headers = NULL;
            g_mutex_unlock(&m->cmd_lock);

            ff_close(ff);

            ring_reset(m);
            __atomic_store_n(&m->eof, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&m->has_stream, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&m->cursor, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&m->length, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&m->cmd_seek, (ma_int64)-1, __ATOMIC_RELEASE);
            m->error[0] = '\0';

            if (url != NULL && ff_open(ff, m, url, headers) == 0) {
                __atomic_store_n(&m->length, ff->length_frames, __ATOMIC_RELEASE);
                __atomic_store_n(&m->has_stream, 1, __ATOMIC_RELEASE);
                __atomic_store_n(&m->state, MEDIA_PLAYING, __ATOMIC_RELEASE);
            } else {
                if (m->error[0] == '\0')
                    set_error(m, "加载失败");
                __atomic_store_n(&m->state, MEDIA_STOPPED, __ATOMIC_RELEASE);
                g_warning("media: %s (%s)", m->error, url != NULL ? url : "?");
            }

            g_free(url);
            g_free(headers);
            continue;
        }

        /* 2) seek */
        seek = __atomic_exchange_n(&m->cmd_seek, (ma_int64)-1, __ATOMIC_ACQ_REL);
        if (seek >= 0 && ff->fmt != NULL) {
            if (ff_seek(ff, (ma_uint64)seek, m)) {
                ring_reset(m);
                __atomic_store_n(&m->eof, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&m->cursor, (ma_uint64)seek, __ATOMIC_RELEASE);
            } else {
                /* seek 失败：位置和缓冲都保持不动，继续放；
                 * 同时把 UI 那边的光标拉回真实播放位置，避免进度条乱跳 */
                __atomic_store_n(&m->cmd_seek, (ma_int64)-1, __ATOMIC_RELEASE);
            }
        }

        if (__atomic_load_n(&m->state, __ATOMIC_ACQUIRE) != MEDIA_PLAYING) {
            g_usleep(5000);
            continue;
        }

        if (ff->fmt == NULL || __atomic_load_n(&m->eof, __ATOMIC_ACQUIRE)) {
            g_usleep(10000);
            continue;
        }

        /* 缓冲快满就先歇着，别把网络读得太凶 */
        if (ring_free_frames(m) < MEDIA_DECODE_CHUNK) {
            g_usleep(3000);
            continue;
        }

        if (ff_decode_some(ff, m) < 0)
            __atomic_store_n(&m->eof, 1, __ATOMIC_RELEASE);
    }

    ff_close(ff);
    g_free(ff);
    return NULL;
}

/* ---------- 对外接口 ---------- */

int media_init(Media *m)
{
    ma_device_config config;
    ma_result        result;

    if (m == NULL)
        return -1;
    if (m->ready)
        return 0;

    memset(m, 0, sizeof(*m));

    avformat_network_init();

    config                     = ma_device_config_init(ma_device_type_playback);
    config.playback.format     = ma_format_f32;
    config.playback.channels   = MEDIA_CHANNELS;
    config.sampleRate          = 0;    /* 0 = 用设备原生采样率 */
    config.dataCallback        = media_data_callback;
    config.pUserData           = m;

    result = ma_device_init(NULL, &config, &m->device);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "media: device init failed: %s (%d)\n",
                ma_result_description(result), (int)result);
        return -2;
    }

    m->sample_rate = m->device.sampleRate;
    if (m->sample_rate == 0)
        m->sample_rate = MEDIA_FALLBACK_RATE;

    m->ring_frames = m->sample_rate * MEDIA_BUFFER_SECONDS;
    m->ring = g_new0(float, (gsize)m->ring_frames * MEDIA_CHANNELS);

    m->cmd_seek = -1;
    g_mutex_init(&m->cmd_lock);

    result = ma_device_start(&m->device);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "media: device start failed: %s (%d)\n",
                ma_result_description(result), (int)result);
        ma_device_uninit(&m->device);
        g_mutex_clear(&m->cmd_lock);
        g_free(m->ring);
        m->ring = NULL;
        return -3;
    }

    m->thread = g_thread_new("follow-decode", media_decode_thread, m);
    m->ready  = 1;

    return 0;
}

int media_play(Media *m, const char *url, const char *headers)
{
    if (m == NULL || !m->ready || url == NULL)
        return -1;

    g_mutex_lock(&m->cmd_lock);
    g_free(m->cmd_url);
    g_free(m->cmd_headers);
    m->cmd_url     = g_strdup(url);
    m->cmd_headers = dup_headers_or_null(headers);
    g_mutex_unlock(&m->cmd_lock);

    __atomic_store_n(&m->state, MEDIA_LOADING, __ATOMIC_RELEASE);
    m->error[0] = '\0';        /* 新的一次加载：先清掉上一首留下的错误 */
    __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&m->cursor, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&m->cmd_load, 1, __ATOMIC_RELEASE);

    return 0;
}

int media_pause(Media *m)
{
    if (m == NULL)
        return -1;

    if (__atomic_load_n(&m->state, __ATOMIC_ACQUIRE) == MEDIA_PLAYING)
        __atomic_store_n(&m->state, MEDIA_PAUSED, __ATOMIC_RELEASE);

    return 0;
}

int media_continue(Media *m)
{
    int state;

    if (m == NULL || !__atomic_load_n(&m->has_stream, __ATOMIC_ACQUIRE))
        return -1;

    state = __atomic_load_n(&m->state, __ATOMIC_ACQUIRE);
    if (state == MEDIA_STOPPED) {
        /* 停止状态下继续：从头开始 */
        __atomic_store_n(&m->cmd_seek, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
    }

    __atomic_store_n(&m->state, MEDIA_PLAYING, __ATOMIC_RELEASE);
    return 0;
}

void media_stop(Media *m)
{
    if (m == NULL)
        return;

    __atomic_store_n(&m->state, MEDIA_STOPPED, __ATOMIC_RELEASE);
    __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&m->eof, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&m->cursor, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&m->cmd_seek, 0, __ATOMIC_RELEASE);
}

double media_position(Media *m)
{
    if (m == NULL)
        return 0.0;

    return (double)__atomic_load_n(&m->cursor, __ATOMIC_ACQUIRE) / (double)m->sample_rate;
}

double media_duration(Media *m)
{
    if (m == NULL)
        return 0.0;

    return (double)__atomic_load_n(&m->length, __ATOMIC_ACQUIRE) / (double)m->sample_rate;
}

int media_seek(Media *m, double seconds)
{
    ma_uint64 frame;
    ma_uint64 length;

    if (m == NULL || !__atomic_load_n(&m->has_stream, __ATOMIC_ACQUIRE))
        return -1;
    if (seconds < 0.0)
        seconds = 0.0;

    frame  = (ma_uint64)(seconds * (double)m->sample_rate);
    length = __atomic_load_n(&m->length, __ATOMIC_ACQUIRE);

    if (length > 0 && frame >= length)
        frame = length - 1;

    __atomic_store_n(&m->cursor, frame, __ATOMIC_RELEASE);
    __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&m->cmd_seek, (ma_int64)frame, __ATOMIC_RELEASE);

    return 0;
}

int media_is_playing(Media *m)
{
    return m != NULL && __atomic_load_n(&m->state, __ATOMIC_ACQUIRE) == MEDIA_PLAYING;
}

int media_is_paused(Media *m)
{
    return m != NULL && __atomic_load_n(&m->state, __ATOMIC_ACQUIRE) == MEDIA_PAUSED;
}

int media_is_loading(Media *m)
{
    return m != NULL && __atomic_load_n(&m->state, __ATOMIC_ACQUIRE) == MEDIA_LOADING;
}

void media_ack_finished(Media *m)
{
    if (m != NULL)
        __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
}

void media_begin_track(Media *m)
{
    if (m == NULL)
        return;

    m->error[0] = '\0';
    __atomic_store_n(&m->finished, 0, __ATOMIC_RELEASE);
}

int media_is_finished(Media *m)
{
    return m != NULL && __atomic_load_n(&m->finished, __ATOMIC_ACQUIRE);
}

const char *media_error(Media *m)
{
    if (m == NULL || m->error[0] == '\0')
        return NULL;

    return m->error;
}

void media_clear(Media *m)
{
    if (m == NULL || !m->ready)
        return;

    __atomic_store_n(&m->thread_stop, 1, __ATOMIC_RELEASE);
    g_thread_join(m->thread);
    m->thread = NULL;

    ma_device_uninit(&m->device);

    g_mutex_clear(&m->cmd_lock);
    g_free(m->cmd_url);
    g_free(m->cmd_headers);
    g_free(m->ring);
    m->ring = NULL;

    m->ready = 0;

    avformat_network_deinit();
}
