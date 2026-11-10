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

#include <string.h>

#include <curl/curl.h>

#include <libavutil/error.h>
#include <libavutil/mem.h>

#include "config.h"
#include "httpstream.h"

/* 每次请求取多少字节。
 * 实测 googlevideo 对 rqh=1 的地址有块大小上限：bytes=0-1048575（1MB）是 206，
 * 再大（1.25MB / 2MB / 4MB）就 403，所以上限取 1MB；万一哪天更小，
 * stream_fill() 会自动砍半重试。 */
#define CHUNK_BYTES (1024 * 1024)
#define CHUNK_MIN   (64 * 1024)

typedef struct {
    gchar  *url;
    gchar  *headers;             /* "Key: Value\r\n..."，可为 NULL */
    gchar  *ua;

    gsize   chunk;               /* 当前一次请求的大小（装不下会砍半） */
    guint8 *buf;                 /* 当前这一块的原始数据 */
    gsize   buf_len;
    gsize   buf_pos;
    gint64  buf_off;             /* buf[0] 对应的文件偏移 */

    gint64  offset;              /* 下一次要交付的字节位置 */
    gint64  size;                /* 文件总长，-1 = 还不知道 */
    gboolean eof;
    int      status;             /* 最近一次的 HTTP 状态码 */
    gchar    error[CURL_ERROR_SIZE];
} Stream;

/* ---------- libcurl 回调 ---------- */

static size_t stream_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Stream *s = userdata;
    gsize   n = size * nmemb;

    if (s->buf_len + n > CHUNK_BYTES) {
        n = CHUNK_BYTES - s->buf_len;          /* 服务器没按 Range 返回值：只留一块 */
        if (n == 0)
            return 0;
    }

    memcpy(s->buf + s->buf_len, ptr, n);
    s->buf_len += n;

    return size * nmemb;
}

/* 只关心 Content-Range，里面带着文件总长（bytes a-b/total） */
static size_t stream_header_cb(char *data, size_t size, size_t nmemb, void *userdata)
{
    Stream *s = userdata;
    gsize   n = size * nmemb;

    if (n > 14 && g_ascii_strncasecmp(data, "content-range:", 14) == 0) {
        const char *slash = memchr(data, '/', n);

        if (slash != NULL) {
            g_autofree gchar *tail = g_strndup(slash + 1, n - (gsize)(slash - data) - 1);
            gint64 total = g_ascii_strtoll(g_strstrip(tail), NULL, 10);

            if (total > 0)
                s->size = total;
        }
    }

    return n;
}

/* 发一次请求，取 s->chunk 字节。成功（拿到数据）返回 TRUE */
static gboolean stream_fetch(Stream *s)
{
    CURL              *curl;
    struct curl_slist *list = NULL;
    gchar              range[64];
    gint64             end;
    long               status = 0;
    CURLcode           rc;

    s->buf_len = 0;
    s->buf_pos = 0;
    s->buf_off = s->offset;

    if (s->size >= 0 && s->offset >= s->size)
        return FALSE;                          /* 到末尾了 */

    end = s->offset + (gint64)s->chunk - 1;
    if (s->size > 0 && end > s->size - 1)
        end = s->size - 1;

    g_snprintf(range, sizeof(range), "%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT,
               s->offset, end);

    curl = curl_easy_init();
    if (curl == NULL) {
        g_strlcpy(s->error, "curl_easy_init failed", sizeof(s->error));
        return FALSE;
    }

    curl_easy_setopt(curl, CURLOPT_URL, s->url);
    curl_easy_setopt(curl, CURLOPT_RANGE, range);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)config_http_connect_timeout());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, s->ua);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, stream_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, s);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, s->error);

    if (config_proxy() != NULL)
        curl_easy_setopt(curl, CURLOPT_PROXY, config_proxy());

    if (s->headers != NULL) {
        gchar *copy = g_strdup(s->headers);
        gchar *save = NULL;
        gchar *line;

        for (line = strtok_r(copy, "\r\n", &save); line != NULL;
             line = strtok_r(NULL, "\r\n", &save)) {
            if (*line != '\0')
                list = curl_slist_append(list, line);
        }

        g_free(copy);

        if (list != NULL)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    }

    s->error[0] = '\0';
    rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    s->status = (int)status;

    if (list != NULL)
        curl_slist_free_all(list);

    curl_easy_cleanup(curl);

    /* 206 = 正常的分块响应；200 说明服务器忽略了 Range，
     * 只有从 0 开始取时这份数据才是对的。 */
    if (status != 206 && status != 200)
        return FALSE;

    if (status == 200 && s->buf_off != 0)
        return FALSE;

    if (rc != CURLE_OK && s->buf_len == 0)
        return FALSE;

    return s->buf_len > 0;
}

/* 取一块：块太大被拒（403）就砍半再来，直到成功或缩到 CHUNK_MIN */
static gboolean stream_fill(Stream *s)
{
    for (;;) {
        if (stream_fetch(s))
            return TRUE;

        if (s->status != 403 || s->chunk <= CHUNK_MIN)
            return FALSE;

        g_message("httpstream: %zuKB 的块被拒（HTTP %d），改用 %zuKB 再试",
                  s->chunk / 1024, s->status, s->chunk / 2048);
        s->chunk /= 2;
    }
}

/* ---------- AVIOContext 回调 ---------- */

static int stream_read(void *opaque, uint8_t *out, int want)
{
    Stream *s = opaque;
    int     got = 0;

    while (got < want) {
        gsize avail = s->buf_len - s->buf_pos;

        if (avail == 0) {
            if (s->eof || !stream_fill(s)) {
                s->eof = TRUE;
                break;
            }

            avail = s->buf_len;
        }

        {
            gsize n = MIN(avail, (gsize)(want - got));

            memcpy(out + got, s->buf + s->buf_pos, n);
            s->buf_pos += n;
            s->offset  += (gint64)n;
            got        += (int)n;
        }
    }

    if (got > 0)
        return got;

    return AVERROR_EOF;
}

static int64_t stream_seek(void *opaque, int64_t off, int whence)
{
    Stream *s = opaque;
    gint64  target;

    if (whence == AVSEEK_SIZE)
        return s->size;

    switch (whence) {
    case SEEK_SET: target = off; break;
    case SEEK_CUR: target = s->offset + off; break;
    case SEEK_END:
        if (s->size < 0)
            return AVERROR(EINVAL);
        target = s->size + off;
        break;
    default:
        return AVERROR(EINVAL);
    }

    if (target < 0 || (s->size >= 0 && target > s->size))
        return AVERROR(EINVAL);

    /* 落在已缓冲的范围里就只挪游标，否则作废缓冲，下次读的时候重新取 */
    if (target >= s->buf_off && target <= s->buf_off + (gint64)s->buf_len) {
        s->buf_pos = (gsize)(target - s->buf_off);
    } else {
        s->buf_len = 0;
        s->buf_pos = 0;
        s->buf_off = target;
    }

    s->offset = target;
    s->eof    = FALSE;

    return target;
}

/* ---------- 对外接口 ---------- */

AVIOContext *httpstream_avio_open(const char *url, const char *headers, gchar **error)
{
    Stream       *s;
    AVIOContext  *pb;
    guint8       *iobuf;

    if (error != NULL)
        *error = NULL;

    if (url == NULL || *url == '\0') {
        if (error != NULL)
            *error = g_strdup("网络流地址为空");
        return NULL;
    }

    s          = g_new0(Stream, 1);
    s->url     = g_strdup(url);
    s->headers = (headers != NULL && *headers != '\0') ? g_strdup(headers) : NULL;
    s->ua      = g_strdup(config_user_agent());
    s->buf     = g_malloc(CHUNK_BYTES);
    s->chunk   = CHUNK_BYTES;
    s->size    = -1;

    /* 先取第一块：既探到总长，也让后续读取不用再等网络 */
    if (!stream_fill(s)) {
        if (error != NULL) {
            *error = g_strdup_printf("网络流打不开（HTTP %d%s%s）", s->status,
                                     s->error[0] != '\0' ? "：" : "",
                                     s->error[0] != '\0' ? s->error : "");
        }

        g_free(s->url);
        g_free(s->headers);
        g_free(s->ua);
        g_free(s->buf);
        g_free(s);
        return NULL;
    }

    /* 这块缓冲由 FFmpeg 负责释放，
     * 必须用 av_malloc 系列分配，不能给 g_malloc 的指针。 */
    iobuf = av_malloc(4096);
    pb    = avio_alloc_context(iobuf, 4096, 0, s, stream_read, NULL, stream_seek);

    if (pb == NULL) {
        if (error != NULL)
            *error = g_strdup("分配 AVIOContext 失败");

        av_free(iobuf);
        g_free(s->url);
        g_free(s->headers);
        g_free(s->ua);
        g_free(s->buf);
        g_free(s);
        return NULL;
    }

    pb->seekable = AVIO_SEEKABLE_NORMAL;
    pb->direct   = 1;          /* 我们已经有 2MB 的块缓冲，不用 AVIO 再包一层 */

    g_debug("httpstream: %s (%" G_GINT64_FORMAT " bytes)", url, s->size);

    return pb;
}

void httpstream_avio_free(AVIOContext **pb)
{
    AVIOContext *p;
    Stream      *s;

    if (pb == NULL || *pb == NULL)
        return;

    p = *pb;
    s = p->opaque;

    if (s != NULL) {
        g_free(s->url);
        g_free(s->headers);
        g_free(s->ua);
        g_free(s->buf);
        g_free(s);
        p->opaque = NULL;
    }

    av_freep(&p->buffer);
    avio_context_free(&p);
    *pb = NULL;
}
