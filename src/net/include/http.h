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

#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <curl/curl.h>


typedef struct {
    char  *body;                        /* 响应正文，NUL 结尾；用 http_response_free 释放 */
    size_t length;                      /* 正文字节数（不含结尾 NUL） */
    long   status;                      /* HTTP 状态码，0 表示传输层就没成功 */
    char   error[CURL_ERROR_SIZE];      /* 失败原因，成功时为空串 */
    char  *final_url;                   /* 跟完重定向后的最终地址，可能为 NULL */
} HttpResponse;

/* 进程内调用一次：main() 开头 / 所有线程启动之前。成功返回 0 */
int  http_global_init(void);

/* 进程内调用一次：main() 结尾。 */
void http_global_cleanup(void);

/*
 * 同步 GET。成功返回 0。
 * timeout_sec <= 0 时用 [net] timeout（默认 15 秒）。
 * UA / 建连超时均取自 [net] 配置，未配置时用 config.h 里的内置值。
 * 无论成功失败，resp 里的 body（可能为 ""）都要用 http_response_free 释放。
 */
int  http_get(const char *url, HttpResponse *resp, long timeout_sec);

/*
 * 同步 GET，但可以带额外请求头。
 */
int  http_get_with_headers(const char *url, const char *headers,
                           HttpResponse *resp, long timeout_sec);

/*
 * 同步 POST
 * 其余语义和返回值同 http_get_with_headers。
 */
int  http_post_with_headers(const char *url, const char *body, const char *headers,
                            HttpResponse *resp, long timeout_sec);

/* 释放 body 并把结构体清零，可重复调用 */
void http_response_free(HttpResponse *resp);

#endif /* HTTP_H */
