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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#endif

#include "http.h"
#include "config.h"

#if defined(_WIN32)
static char *g_ca_bundle;

static char *http_find_bundled_ca(void)
{
    wchar_t  exe[MAX_PATH];
    wchar_t  dir[MAX_PATH];
    wchar_t *slash;
    DWORD    n;
    static const wchar_t *candidates[] = {
        L"\\etc\\ssl\\certs\\ca-bundle.crt",
        L"\\etc\\ssl\\cert.pem",
    };

    n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return NULL;

    wcscpy(dir, exe);
    slash = wcsrchr(dir, L'\\');
    if (slash == NULL)
        return NULL;
    *slash = L'\0';

    for (gsize i = 0; i < G_N_ELEMENTS(candidates); i++) {
        wchar_t  path_w[MAX_PATH];
        char    *path;
        int      len;

        wcsncpy(path_w, dir, MAX_PATH - 1);
        path_w[MAX_PATH - 1] = L'\0';
        wcsncat(path_w, candidates[i], MAX_PATH - wcslen(path_w) - 1);

        if (GetFileAttributesW(path_w) == INVALID_FILE_ATTRIBUTES)
            continue;

        len = WideCharToMultiByte(CP_UTF8, 0, path_w, -1, NULL, 0, NULL, NULL);
        if (len <= 0)
            continue;

        path = g_malloc((gsize)len);
        WideCharToMultiByte(CP_UTF8, 0, path_w, -1, path, len, NULL, NULL);

        _putenv_s("CURL_CA_BUNDLE", path);
        _putenv_s("SSL_CERT_FILE", path);
        return path;
    }

    return NULL;
}
#endif

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

/* 通用请求实现；method == NULL 时按 GET 处理。 */
static int http_perform(const char *method, const char *url,
                        const void *body, size_t body_len, const char *headers,
                        const char *userpwd, HttpResponse *resp, long timeout_sec);

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
#if defined(_WIN32)
    if (getenv("CURL_CA_BUNDLE") == NULL && getenv("SSL_CERT_FILE") == NULL)
        g_ca_bundle = http_find_bundled_ca();
#endif

    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void http_global_cleanup(void)
{
    curl_global_cleanup();
#if defined(_WIN32)
    g_free(g_ca_bundle);
    g_ca_bundle = NULL;
#endif
}

static int http_perform(const char *method, const char *url,
                        const void *body, size_t body_len, const char *headers,
                        const char *userpwd, HttpResponse *resp, long timeout_sec)
{
    Buffer            buf = {0};
    CURL             *curl;
    CURLcode          rc;
    struct curl_slist *list = NULL;
    long              timeout;

    if (url == NULL || resp == NULL)
        return -1;

    memset(resp, 0, sizeof(*resp));

    /* 每次调用读取配置；timeout_sec > 0 可覆盖。 */
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

    /* headers 已含 User-Agent 时不再设置 CURLOPT_USERAGENT。 */
    {
        gboolean ua_in_headers = FALSE;

        if (headers != NULL) {
            g_autofree gchar *lower = g_ascii_strdown(headers, -1);

            ua_in_headers = strstr(lower, "user-agent:") != NULL;
        }

        if (!ua_in_headers)
            curl_easy_setopt(curl, CURLOPT_USERAGENT, config_user_agent());
    }

    if (method != NULL && g_ascii_strcasecmp(method, "GET") != 0) {
        if (g_ascii_strcasecmp(method, "POST") == 0)
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        else
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    }

    if (method != NULL && g_ascii_strcasecmp(method, "HEAD") == 0)
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    if (body != NULL) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }

    if (userpwd != NULL && *userpwd != '\0') {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_ANY);
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);
    }

#if defined(_WIN32)
    if (g_ca_bundle != NULL)
        curl_easy_setopt(curl, CURLOPT_CAINFO, g_ca_bundle);
    else if (getenv("CURL_CA_BUNDLE") == NULL && getenv("SSL_CERT_FILE") == NULL)
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif

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

int http_request_ex(const char *method, const char *url,
                    const void *body, size_t body_len,
                    const char *headers, const char *userpwd,
                    HttpResponse *resp, long timeout_sec)
{
    return http_perform(method, url, body, body_len, headers, userpwd,
                        resp, timeout_sec);
}

int http_get_with_headers(const char *url, const char *headers,
                          HttpResponse *resp, long timeout_sec)
{
    return http_perform("GET", url, NULL, 0, headers, NULL, resp, timeout_sec);
}

int http_post_with_headers(const char *url, const char *body, const char *headers,
                           HttpResponse *resp, long timeout_sec)
{
    return http_perform("POST", url, body, body ? strlen(body) : 0,
                        headers, NULL, resp, timeout_sec);
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
