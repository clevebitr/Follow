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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "config.h"

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

/* GET / POST 的公共实现：body == NULL 就是 GET */
static int http_request(const char *url, const char *body, const char *headers,
                        HttpResponse *resp, long timeout_sec);

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Buffer *buf = (Buffer *)userdata;
    size_t  n   = size * nmemb;

    if (buf->len + n + 1 > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 1024;
        char  *p;

        while (cap < buf->len + n + 1)
            cap *= 2;

        p = (char *)realloc(buf->data, cap);
        if (p == NULL)
            return 0;

        buf->data = p;
        buf->cap  = cap;
    }

    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = '\0';

    return n;
}

int http_global_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void http_global_cleanup(void)
{
    curl_global_cleanup();
}

static int http_request(const char *url, const char *body, const char *headers,
                        HttpResponse *resp, long timeout_sec)
{
    Buffer            buf = {0};
    CURL             *curl;
    CURLcode          rc;
    struct curl_slist *list = NULL;
    long              timeout;

    if (url == NULL || resp == NULL)
        return -1;

    memset(resp, 0, sizeof(*resp));

    /* 每次调用都现取配置：ini 里写了就用，没写回落到内置默认
     * （timeout_sec > 0 的调用方仍可单次覆盖） */
    timeout = timeout_sec > 0 ? timeout_sec : config_http_timeout();

    curl = curl_easy_init();
    if (curl == NULL) {
        snprintf(resp->error, sizeof(resp->error), "curl_easy_init failed");
        return -2;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)config_http_connect_timeout());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

    if (config_proxy() != NULL)
        curl_easy_setopt(curl, CURLOPT_PROXY, config_proxy());

    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config_user_agent());

    if (body != NULL) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, resp->error);

    if (headers != NULL && *headers != '\0') {
        char *copy = strdup(headers);
        char *save = NULL;
        char *line;

        for (line = strtok_r(copy, "\r\n", &save); line != NULL;
             line = strtok_r(NULL, "\r\n", &save)) {
            if (*line != '\0')
                list = curl_slist_append(list, line);
        }

        free(copy);

        if (list != NULL)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    rc = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->status);

    {
        char *effective = NULL;

        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
        if (effective != NULL)
            resp->final_url = strdup(effective);
    }

    if (list != NULL)
        curl_slist_free_all(list);

    curl_easy_cleanup(curl);

    resp->body   = buf.data ? buf.data : (char *)calloc(1, 1);
    resp->length = buf.len;

    if (rc != CURLE_OK) {
        if (resp->error[0] == '\0')
            snprintf(resp->error, sizeof(resp->error), "%s", curl_easy_strerror(rc));
        return -3;
    }

    return 0;
}

int http_get_with_headers(const char *url, const char *headers,
                          HttpResponse *resp, long timeout_sec)
{
    return http_request(url, NULL, headers, resp, timeout_sec);
}

int http_post_with_headers(const char *url, const char *body, const char *headers,
                           HttpResponse *resp, long timeout_sec)
{
    return http_request(url, body, headers, resp, timeout_sec);
}

int http_get(const char *url, HttpResponse *resp, long timeout_sec)
{
    return http_get_with_headers(url, NULL, resp, timeout_sec);
}

void http_response_free(HttpResponse *resp)
{
    if (resp == NULL)
        return;

    free(resp->body);
    free(resp->final_url);
    memset(resp, 0, sizeof(*resp));
}
