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

#ifndef FOLLOW_BACKUP_H
#define FOLLOW_BACKUP_H

#include <glib.h>

/* WebDAV 备份编排：config + stats.dat + settings.ini。
 * 网络请求在工作线程执行，UI 通过 backup_version / backup_state 轮询。 */

typedef enum {
    BACKUP_OP_TEST = 0,
    BACKUP_OP_BACKUP,
    BACKUP_OP_RESTORE
} BackupOp;

typedef enum {
    BACKUP_IDLE = 0,
    BACKUP_RUNNING,
    BACKUP_OK,
    BACKUP_ERROR
} BackupState;

gboolean    backup_start(BackupOp op);
BackupState backup_state(void);
BackupOp    backup_last_op(void);
gchar      *backup_status_text(void);   /* 新分配 */
guint       backup_version(void);
gboolean    backup_busy(void);

/* 退出时等待自动备份完成；超时返回 FALSE。 */
gboolean    backup_wait(guint timeout_ms);
void        backup_cancel(void);
void        backup_shutdown(void);

/* 读取 webdav.ini：auto_backup 且 URL 有效时返回 TRUE。 */
gboolean    backup_auto_enabled(void);

#endif /* FOLLOW_BACKUP_H */
