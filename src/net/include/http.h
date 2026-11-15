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

#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <curl/curl.h>

typedef struct {
    char  *body;                        /* 响应正文；http_response_free 释放。 */
    size_t length;
    long   status;
    char   error[CURL_ERROR_SIZE];
    char  *final_url;
} HttpResponse;

/* 进程内初始化 / 清理；须在所有线程启动前调用。 */
int  http_global_init(void);

void http_global_cleanup(void);

/* 同步 GET；timeout_sec <= 0 时使用配置值。body 须用 http_response_free 释放。 */
int  http_get(const char *url, HttpResponse *resp, long timeout_sec);

/* 带额外请求头的同步 GET。 */
int  http_get_with_headers(const char *url, const char *headers,
                           HttpResponse *resp, long timeout_sec);

/* 同步 POST；语义同 http_get_with_headers。 */
int  http_post_with_headers(const char *url, const char *body, const char *headers,
                            HttpResponse *resp, long timeout_sec);

/* 通用同步请求：方法 / 二进制 body / 可选 Basic/Digest 认证。 */
int  http_request_ex(const char *method, const char *url,
                     const void *body, size_t body_len,
                     const char *headers, const char *userpwd,
                     HttpResponse *resp, long timeout_sec);

/* 释放响应；可重复调用。 */
void http_response_free(HttpResponse *resp);

#endif
