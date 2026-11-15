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

#include "backup.h"

#include <string.h>

#include <glib/gstdio.h>

#include "config.h"
#include "settings.h"
#include "stats.h"
#include "webdav.h"
#include "webdavsettings.h"

typedef struct {
    BackupOp  op;
    guint     gen;
    gchar    *url;
    gchar    *username;
    gchar    *password;
    gchar    *config_path;
    gchar    *stats_path;
    gchar    *settings_path;
} BackupJob;

static struct {
    GMutex      lock;
    BackupState state;
    BackupOp    last_op;
    gchar      *status;
    guint       version;
    gboolean    busy;
    gboolean    cancel;
    guint       gen;
} B = { .state = BACKUP_IDLE, .last_op = BACKUP_OP_TEST };

static void state_set(BackupState state, const gchar *status)
{
    g_mutex_lock(&B.lock);
    B.state = state;
    g_free(B.status);
    B.status = g_strdup(status != NULL ? status : "");
    B.busy = (state == BACKUP_RUNNING);
    B.version++;
    g_mutex_unlock(&B.lock);
}

static gboolean is_cancelled(void)
{
    gboolean cancelled;

    g_mutex_lock(&B.lock);
    cancelled = B.cancel;
    g_mutex_unlock(&B.lock);

    return cancelled;
}

gboolean backup_auto_enabled(void)
{
    WebDavSettings settings;
    gboolean       enabled;

    webdav_settings_load(&settings);
    enabled = settings.auto_backup && webdav_settings_valid(&settings);
    webdav_settings_clear(&settings);

    return enabled;
}

static gboolean upload_file(WebDavClient *client, const gchar *path,
                            const gchar *name, gsize *count, gchar **error)
{
    g_autofree gchar *data = NULL;
    gsize             length = 0;

    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return TRUE;

    if (!g_file_get_contents(path, &data, &length, NULL)) {
        if (error != NULL)
            *error = g_strdup_printf("无法读取 %s", path);
        return FALSE;
    }

    if (!webdav_put(client, name, data, length, error))
        return FALSE;

    (*count)++;
    return TRUE;
}

static gboolean download_file(WebDavClient *client, const gchar *name,
                              const gchar *path, gsize *count,
                              gboolean *missing, gchar **error)
{
    g_autofree gchar *data = NULL;
    g_autofree gchar *dir = NULL;
    gsize             length = 0;
    gboolean          miss = FALSE;

    if (!webdav_get(client, name, &data, &length, &miss, error))
        return FALSE;

    if (miss) {
        *missing = TRUE;
        return TRUE;
    }

    dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        if (error != NULL)
            *error = g_strdup_printf("无法创建目录 %s", dir);
        return FALSE;
    }

    if (!g_file_set_contents(path, data, (gssize)length, NULL)) {
        if (error != NULL)
            *error = g_strdup_printf("无法写入 %s", path);
        return FALSE;
    }

    g_chmod(path, 0600);
    (*count)++;
    return TRUE;
}

static void mark_backup_done(void)
{
    WebDavSettings settings;

    webdav_settings_load(&settings);
    settings.last_backup = g_get_real_time() / G_USEC_PER_SEC;
    webdav_settings_save(&settings);
    webdav_settings_clear(&settings);
}

static gpointer backup_worker(gpointer data)
{
    BackupJob  *job = data;
    WebDavClient client;
    g_autofree gchar *error = NULL;
    gsize       count = 0;
    gsize       attempted = 0;
    gboolean    missing = FALSE;
    gboolean    ok = FALSE;
    gchar      *status = NULL;

    webdav_client_init(&client, job->url, job->username, job->password);

    if (is_cancelled()) {
        status = g_strdup("已取消");
        goto done;
    }

    if (job->op == BACKUP_OP_TEST) {
        ok = webdav_test(&client, &error);
        status = ok ? g_strdup("连接成功")
                    : g_strdup_printf("连接失败：%s",
                                      error != NULL ? error : "unknown");
        goto done;
    }

    if (!webdav_test(&client, &error)) {
        status = g_strdup_printf("连接失败：%s",
                                 error != NULL ? error : "unknown");
        goto done;
    }

    if (job->op == BACKUP_OP_BACKUP) {
        attempted = 3;

        if (!upload_file(&client, job->config_path, "config.ini", &count, &error) ||
            !upload_file(&client, job->stats_path, "stats.dat", &count, &error) ||
            !upload_file(&client, job->settings_path, "settings.ini", &count, &error)) {
            status = g_strdup_printf("备份失败：%s",
                                     error != NULL ? error : "unknown");
            goto done;
        }

        if (count == 0) {
            status = g_strdup("没有可备份的文件");
            goto done;
        }

        mark_backup_done();
        ok = TRUE;
        status = g_strdup_printf("备份完成（%" G_GSIZE_FORMAT " 个文件）", count);
        goto done;
    }

    attempted = 3;

    if (!download_file(&client, "config.ini", job->config_path, &count,
                       &missing, &error) ||
        !download_file(&client, "stats.dat", job->stats_path, &count,
                       &missing, &error) ||
        !download_file(&client, "settings.ini", job->settings_path, &count,
                       &missing, &error)) {
        status = g_strdup_printf("恢复失败：%s",
                                 error != NULL ? error : "unknown");
        goto done;
    }

    if (count == 0) {
        status = g_strdup("远端没有可恢复的文件");
        goto done;
    }

    ok = TRUE;
    status = g_strdup_printf("恢复完成（%" G_GSIZE_FORMAT " 个文件）", count);

done:
    (void)attempted;
    webdav_client_clear(&client);

    if (is_cancelled()) {
        g_free(status);
        status = g_strdup("已取消");
        ok = FALSE;
    }

    state_set(ok ? BACKUP_OK : BACKUP_ERROR,
              status != NULL ? status : "同步失败");

    g_free(status);
    g_free(job->url);
    g_free(job->username);
    g_free(job->password);
    g_free(job->config_path);
    g_free(job->stats_path);
    g_free(job->settings_path);
    g_free(job);
    return NULL;
}

gboolean backup_start(BackupOp op)
{
    WebDavSettings settings;
    g_autofree gchar *password = NULL;
    g_autofree gchar *error = NULL;
    BackupJob *job;
    const gchar *running;

    g_mutex_lock(&B.lock);
    if (B.busy) {
        g_mutex_unlock(&B.lock);
        return FALSE;
    }
    B.cancel = FALSE;
    B.gen++;
    g_mutex_unlock(&B.lock);

    webdav_settings_load(&settings);

    if (!webdav_settings_valid(&settings)) {
        webdav_settings_clear(&settings);
        state_set(BACKUP_ERROR, "WebDAV 未配置或地址无效");
        return FALSE;
    }

    password = webdav_settings_load_password(&settings, &error);
    if (password == NULL && error != NULL)
        g_debug("backup: keyring: %s", error);

    job = g_new0(BackupJob, 1);
    job->op            = op;
    job->url           = g_strdup(settings.url);
    job->username      = g_strdup(settings.username);
    job->password      = g_strdup(password != NULL ? password : "");
    job->config_path   = g_strdup(config_active_path());
    job->stats_path    = stats_file_path();
    job->settings_path = settings_file_path();

    webdav_settings_clear(&settings);

    g_mutex_lock(&B.lock);
    job->gen   = B.gen;
    B.last_op  = op;
    g_mutex_unlock(&B.lock);

    running = op == BACKUP_OP_TEST   ? "正在测试连接…" :
              op == BACKUP_OP_BACKUP ? "正在备份…" : "正在恢复…";
    state_set(BACKUP_RUNNING, running);

    g_thread_unref(g_thread_new("follow-webdav", backup_worker, job));
    return TRUE;
}

BackupState backup_state(void)
{
    BackupState state;

    g_mutex_lock(&B.lock);
    state = B.state;
    g_mutex_unlock(&B.lock);

    return state;
}

BackupOp backup_last_op(void)
{
    BackupOp op;

    g_mutex_lock(&B.lock);
    op = B.last_op;
    g_mutex_unlock(&B.lock);

    return op;
}

gchar *backup_status_text(void)
{
    gchar *text;

    g_mutex_lock(&B.lock);
    text = g_strdup(B.status != NULL ? B.status : "");
    g_mutex_unlock(&B.lock);

    return text;
}

guint backup_version(void)
{
    guint version;

    g_mutex_lock(&B.lock);
    version = B.version;
    g_mutex_unlock(&B.lock);

    return version;
}

gboolean backup_busy(void)
{
    gboolean busy;

    g_mutex_lock(&B.lock);
    busy = B.busy;
    g_mutex_unlock(&B.lock);

    return busy;
}

gboolean backup_wait(guint timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() +
                      (gint64)timeout_ms * 1000;

    while (backup_busy()) {
        if (g_get_monotonic_time() >= deadline)
            return FALSE;

        g_usleep(50 * 1000);
    }

    return TRUE;
}

void backup_cancel(void)
{
    g_mutex_lock(&B.lock);
    B.cancel = TRUE;
    g_mutex_unlock(&B.lock);
}

void backup_shutdown(void)
{
    backup_cancel();
    g_mutex_lock(&B.lock);
    B.busy = FALSE;
    g_mutex_unlock(&B.lock);
}
