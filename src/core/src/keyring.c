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

#include "keyring.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#elif defined(__linux__)
#include <libsecret/secret.h>
#endif

static gchar *target_name(const gchar *service, const gchar *account)
{
    return g_strdup_printf("%s/%s",
                           service != NULL ? service : "Follow",
                           account != NULL ? account : "");
}

#if defined(_WIN32)
static wchar_t *to_wide(const gchar *text, gchar **error)
{
    GError *e = NULL;
    glong   items = 0;
    gunichar2 *wide = g_utf8_to_utf16(text != NULL ? text : "", -1,
                                      NULL, &items, &e);

    if (wide == NULL) {
        if (error != NULL)
            *error = g_strdup(e != NULL ? e->message : "UTF-8 转换失败");
        g_clear_error(&e);
        return NULL;
    }

    return (wchar_t *)wide;
}

gboolean keyring_store(const gchar *service, const gchar *account,
                       const gchar *secret, gchar **error)
{
    g_autofree gchar   *target = target_name(service, account);
    g_autofree wchar_t *target_w = to_wide(target, error);
    g_autofree wchar_t *account_w = to_wide(account, error);
    CREDENTIALW cred = { 0 };

    if (error != NULL)
        *error = NULL;

    if (target_w == NULL || account_w == NULL) {
        if (error != NULL && *error == NULL)
            *error = g_strdup("密钥环参数转换失败");
        return FALSE;
    }

    if (secret == NULL)
        secret = "";

    cred.Type                 = CRED_TYPE_GENERIC;
    cred.TargetName           = target_w;
    cred.CredentialBlobSize   = (DWORD)strlen(secret);
    cred.CredentialBlob       = (LPBYTE)secret;
    cred.Persist              = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName             = account_w;

    if (!CredWriteW(&cred, 0)) {
        if (error != NULL)
            *error = g_strdup_printf("CredWrite 失败：%lu", GetLastError());
        return FALSE;
    }

    return TRUE;
}

gchar *keyring_lookup(const gchar *service, const gchar *account,
                      gchar **error)
{
    g_autofree gchar *target = target_name(service, account);
    g_autofree wchar_t *target_w = to_wide(target, error);
    PCREDENTIALW cred = NULL;
    gchar *secret;

    if (error != NULL)
        *error = NULL;

    if (target_w == NULL) {
        if (error != NULL && *error == NULL)
            *error = g_strdup("密钥环参数转换失败");
        return NULL;
    }

    if (!CredReadW(target_w, CRED_TYPE_GENERIC, 0, &cred)) {
        DWORD rc = GetLastError();

        if (error != NULL && rc != ERROR_NOT_FOUND)
            *error = g_strdup_printf("CredRead 失败：%lu", rc);

        return NULL;
    }

    secret = g_strndup((const gchar *)cred->CredentialBlob,
                       (gsize)cred->CredentialBlobSize);
    CredFree(cred);

    return secret;
}

gboolean keyring_delete(const gchar *service, const gchar *account,
                        gchar **error)
{
    g_autofree gchar *target = target_name(service, account);
    g_autofree wchar_t *target_w = to_wide(target, error);

    if (error != NULL)
        *error = NULL;

    if (target_w == NULL) {
        if (error != NULL && *error == NULL)
            *error = g_strdup("密钥环参数转换失败");
        return FALSE;
    }

    if (!CredDeleteW(target_w, CRED_TYPE_GENERIC, 0)) {
        DWORD rc = GetLastError();

        if (rc == ERROR_NOT_FOUND)
            return TRUE;

        if (error != NULL)
            *error = g_strdup_printf("CredDelete 失败：%lu", rc);
        return FALSE;
    }

    return TRUE;
}

#elif defined(__linux__)

static const SecretSchema g_schema = {
    "org.github.clevebitr.follow.webdav",
    SECRET_SCHEMA_NONE,
    {
        { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { NULL, 0 },
    }
};

gboolean keyring_store(const gchar *service, const gchar *account,
                       const gchar *secret, gchar **error)
{
    GError *e = NULL;
    gboolean ok;

    if (error != NULL)
        *error = NULL;

    ok = secret_password_store_sync(&g_schema, SECRET_COLLECTION_DEFAULT,
                                    "Follow WebDAV",
                                    secret != NULL ? secret : "",
                                    NULL, &e,
                                    "service", service,
                                    "account", account, NULL);

    if (!ok) {
        if (error != NULL)
            *error = g_strdup(e != NULL ? e->message : "密钥环写入失败");
        g_clear_error(&e);
    }

    return ok;
}

gchar *keyring_lookup(const gchar *service, const gchar *account,
                      gchar **error)
{
    GError *e = NULL;
    gchar  *secret = secret_password_lookup_sync(&g_schema, NULL, &e,
                                                 "service", service,
                                                 "account", account, NULL);

    if (error != NULL)
        *error = NULL;

    if (e != NULL) {
        if (error != NULL)
            *error = g_strdup(e->message);
        g_clear_error(&e);
    }

    return secret;
}

gboolean keyring_delete(const gchar *service, const gchar *account,
                        gchar **error)
{
    GError *e = NULL;
    gboolean ok = secret_password_clear_sync(&g_schema, NULL, &e,
                                             "service", service,
                                             "account", account, NULL);

    if (error != NULL)
        *error = NULL;

    if (!ok && e != NULL) {
        if (error != NULL)
            *error = g_strdup(e->message);
        g_clear_error(&e);
    }

    return ok;
}

#else

gboolean keyring_store(const gchar *service, const gchar *account,
                       const gchar *secret, gchar **error)
{
    (void)service;
    (void)account;
    (void)secret;

    if (error != NULL)
        *error = g_strdup("当前平台不支持系统密钥环");
    return FALSE;
}

gchar *keyring_lookup(const gchar *service, const gchar *account,
                      gchar **error)
{
    (void)service;
    (void)account;

    if (error != NULL)
        *error = g_strdup("当前平台不支持系统密钥环");
    return NULL;
}

gboolean keyring_delete(const gchar *service, const gchar *account,
                        gchar **error)
{
    (void)service;
    (void)account;

    if (error != NULL)
        *error = g_strdup("当前平台不支持系统密钥环");
    return FALSE;
}

#endif
