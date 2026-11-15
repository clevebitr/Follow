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

#ifndef FOLLOW_STATS_H
#define FOLLOW_STATS_H

#include <glib.h>

/* 收听统计：按天累计播放秒数，用于热力图。 */

typedef struct {
    GHashTable *days;
    guint       total;
} Stats;

void     stats_init(Stats *st);
void     stats_clear(Stats *st);

gchar   *stats_file_path(void);

gboolean stats_load(Stats *st);
gboolean stats_save(const Stats *st);

void     stats_add(Stats *st, double seconds);

guint    stats_seconds_today(const Stats *st);
guint    stats_total_seconds(const Stats *st);

guint    stats_seconds_days_ago(const Stats *st, gint days_ago);

guint    stats_max_in_range(const Stats *st, gint days);

#endif
