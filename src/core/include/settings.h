/**
 * created: 2026/11/11
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_SETTINGS_H
#define FOLLOW_SETTINGS_H

#include <glib.h>

/* 运行时设置（音量 / 静音 / 输出设备），落在 <cache>/follow/settings.ini，
 * 和用户的 src/config.ini 分开：重新加载 ini 不会冲掉音量记忆。 */

void        settings_load(void);
void        settings_save(void);
void        settings_clear(void);

double      settings_volume(void);
void        settings_set_volume(double volume);

gboolean    settings_muted(void);
void        settings_set_muted(gboolean muted);

const gchar *settings_output_device(void);   /* NULL = 系统默认 */
void        settings_set_output_device(const gchar *name);

#endif /* FOLLOW_SETTINGS_H */
