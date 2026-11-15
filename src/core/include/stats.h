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

#ifndef FOLLOW_STATS_H
#define FOLLOW_STATS_H

#include <glib.h>

/* 收听统计：按天累计播放秒数，用来画左下角的热力图。
 * 存成一个纯文本文件。 */

typedef struct {
    GHashTable *days;      /* "YYYY-MM-DD" -> guint* */
    guint       total;     /* 累计秒数 */
} Stats;

void     stats_init(Stats *st);
void     stats_clear(Stats *st);

gboolean stats_load(Stats *st);
gboolean stats_save(const Stats *st);

/* 累加一段收听的秒数 */
void     stats_add(Stats *st, double seconds);

guint    stats_seconds_today(const Stats *st);
guint    stats_total_seconds(const Stats *st);

/* 取"今天往前第 days_ago 天"的秒数（0 = 今天） */
guint    stats_seconds_days_ago(const Stats *st, gint days_ago);

/* 最近 n 天的最大值，用来给热力图分档 */
guint    stats_max_in_range(const Stats *st, gint days);

#endif /* FOLLOW_STATS_H */
