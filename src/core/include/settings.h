/**
 * created: 2026/11/11
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_SETTINGS_H
#define FOLLOW_SETTINGS_H

#include <glib.h>

/* 运行时设置（音量 / 静音 / 输出设备），存于 <cache>/follow/settings.ini。 */

void        settings_load(void);
void        settings_save(void);
void        settings_clear(void);

double      settings_volume(void);
void        settings_set_volume(double volume);

gboolean    settings_muted(void);
void        settings_set_muted(gboolean muted);

const gchar *settings_output_device(void);
void        settings_set_output_device(const gchar *name);

#endif
