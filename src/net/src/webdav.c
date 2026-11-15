/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#include "webdav.h"

#include <string.h>

#include "http.h"

static gchar *trim_base(const gchar *base_url)
{
    gchar *base;

    if (base_url == NULL)
        return NULL;

    base = g_strdup(base_url);
    g_strstrip(base);

    while (*base != '\0' && base[strlen(base) - 1] == '/')
        base[strlen(base) - 1] = '\0';

    return base;
}

void webdav_client_init(WebDavClient *client, const gchar *base_url,
                        const gchar *username, const gchar *password)
{
    if (client == NULL)
        return;

    client->base_url = trim_base(base_url);

    if (username != NULL && *username != '\0')
        client->auth = g_strdup_printf("%s:%s", username,
                                       password != NULL ? password : "");
    else
        client->auth = NULL;
}

void webdav_client_clear(WebDavClient *client)
{
    if (client == NULL)
        return;

    g_free(client->base_url);
    g_free(client->auth);
    *client = (WebDavClient){ 0 };
}

static gchar *file_url(const WebDavClient *client, const gchar *name)
{
    if (client == NULL || client->base_url == NULL)
        return NULL;

    return g_strdup_printf("%s/%s", client->base_url, name);
}

static gchar *status_error(const HttpResponse *resp)
{
    if (resp->status == 401 || resp->status == 403)
        return g_strdup("WebDAV 认证失败或无权限");

    if (resp->status == 404)
        return g_strdup("WebDAV 路径不存在");

    if (resp->status == 507)
        return g_strdup("WebDAV 存储空间不足");

    if (resp->error[0] != '\0')
        return g_strdup(resp->error);

    return g_strdup_printf("WebDAV HTTP %ld", resp->status);
}

static gboolean ensure_base(WebDavClient *client, gchar **error)
{
    g_autofree gchar *url = NULL;
    HttpResponse resp = { 0 };
    gboolean ok;

    if (client == NULL || client->base_url == NULL) {
        if (error != NULL)
            *error = g_strdup("WebDAV 地址为空");
        return FALSE;
    }

    url = g_strdup_printf("%s/", client->base_url);
    http_request_ex("MKCOL", url, NULL, 0, "Content-Length: 0\r\n",
                    client->auth, &resp, 20);

    ok = resp.status == 200 || resp.status == 201 ||
         resp.status == 204 || resp.status == 405;

    if (!ok && error != NULL)
        *error = status_error(&resp);

    http_response_free(&resp);
    return ok;
}

gboolean webdav_test(WebDavClient *client, gchar **error)
{
    if (error != NULL)
        *error = NULL;

    return ensure_base(client, error);
}

gboolean webdav_put(WebDavClient *client, const gchar *name,
                    const void *data, gsize length, gchar **error)
{
    g_autofree gchar *url = NULL;
    HttpResponse resp = { 0 };
    gboolean ok;

    if (error != NULL)
        *error = NULL;

    if (!ensure_base(client, error))
        return FALSE;

    url = file_url(client, name);
    if (url == NULL) {
        if (error != NULL)
            *error = g_strdup("WebDAV 地址为空");
        return FALSE;
    }

    http_request_ex("PUT", url, data, length,
                    "Content-Type: application/octet-stream\r\nExpect:\r\n",
                    client->auth, &resp, 30);

    ok = resp.status == 200 || resp.status == 201 || resp.status == 204;

    if (!ok && error != NULL)
        *error = status_error(&resp);

    http_response_free(&resp);
    return ok;
}

gboolean webdav_get(WebDavClient *client, const gchar *name,
                    gchar **data, gsize *length, gboolean *missing,
                    gchar **error)
{
    g_autofree gchar *url = NULL;
    HttpResponse resp = { 0 };
    gboolean ok;

    if (data != NULL)
        *data = NULL;
    if (length != NULL)
        *length = 0;
    if (missing != NULL)
        *missing = FALSE;
    if (error != NULL)
        *error = NULL;

    url = file_url(client, name);
    if (url == NULL) {
        if (error != NULL)
            *error = g_strdup("WebDAV 地址为空");
        return FALSE;
    }

    http_request_ex("GET", url, NULL, 0, NULL, client->auth, &resp, 30);

    if (resp.status == 404) {
        if (missing != NULL)
            *missing = TRUE;
        http_response_free(&resp);
        return TRUE;
    }

    ok = resp.status == 200;
    if (ok) {
        if (data != NULL)
            *data = g_memdup2(resp.body, resp.length + 1);
        if (length != NULL)
            *length = resp.length;
    } else if (error != NULL) {
        *error = status_error(&resp);
    }

    http_response_free(&resp);
    return ok;
}
