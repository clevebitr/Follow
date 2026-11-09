#include <string.h>

#include "cache.h"
#include "stats.h"

void stats_init(Stats *st)
{
    if (st == NULL)
        return;

    memset(st, 0, sizeof(*st));
    st->days = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

void stats_clear(Stats *st)
{
    if (st == NULL || st->days == NULL)
        return;

    g_hash_table_destroy(st->days);
    st->days = NULL;
    st->total = 0;
}

static gchar *stats_path(void)
{
    g_autofree gchar *dir = follow_cache_dir();

    return g_build_filename(dir, "stats.dat", NULL);
}

gboolean stats_load(Stats *st)
{
    g_autofree gchar *path = stats_path();
    g_autofree gchar *content = NULL;
    gchar           **lines;

    if (st == NULL || st->days == NULL)
        return FALSE;

    if (!g_file_get_contents(path, &content, NULL, NULL))
        return FALSE;

    lines = g_strsplit(content, "\n", -1);

    for (gchar **line = lines; *line != NULL; line++) {
        gchar **parts = g_strsplit(g_strstrip(*line), " ", 2);

        if (parts[0] != NULL && parts[1] != NULL && *parts[0] != '\0') {
            guint seconds = (guint)MAX(0, g_ascii_strtoll(parts[1], NULL, 10));

            if (seconds > 0) {
                guint *slot = g_new0(guint, 1);

                *slot = seconds;
                g_hash_table_replace(st->days, g_strdup(parts[0]), slot);
                st->total += seconds;
            }
        }

        g_strfreev(parts);
    }

    g_strfreev(lines);

    return TRUE;
}

gboolean stats_save(const Stats *st)
{
    g_autofree gchar *path = NULL;
    GString          *out;
    GHashTableIter    iter;
    gpointer          key, value;
    gboolean          ok;

    if (st == NULL || st->days == NULL)
        return FALSE;

    path = stats_path();
    out  = g_string_new(NULL);

    g_hash_table_iter_init(&iter, st->days);
    while (g_hash_table_iter_next(&iter, &key, &value))
        g_string_append_printf(out, "%s %u\n", (const gchar *)key, *(guint *)value);

    ok = g_file_set_contents(path, out->str, (gssize)out->len, NULL);
    g_string_free(out, TRUE);

    return ok;
}

static gchar *date_key(gint days_ago)
{
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autoptr(GDateTime) day = g_date_time_add_days(now, -days_ago);

    return g_date_time_format(day, "%Y-%m-%d");
}

void stats_add(Stats *st, double seconds)
{
    g_autofree gchar *key = NULL;
    guint            *slot;
    guint             add;

    if (st == NULL || st->days == NULL || seconds <= 0.0)
        return;

    add = (guint)(seconds + 0.5);
    if (add == 0)
        return;

    key = date_key(0);
    slot = g_hash_table_lookup(st->days, key);

    if (slot == NULL) {
        slot = g_new0(guint, 1);
        g_hash_table_replace(st->days, g_strdup(key), slot);
    }

    *slot += add;
    st->total += add;
}

guint stats_seconds_days_ago(const Stats *st, gint days_ago)
{
    g_autofree gchar *key = NULL;
    guint            *slot;

    if (st == NULL || st->days == NULL)
        return 0;

    key  = date_key(days_ago);
    slot = g_hash_table_lookup(st->days, key);

    return slot != NULL ? *slot : 0;
}

guint stats_seconds_today(const Stats *st)
{
    return stats_seconds_days_ago(st, 0);
}

guint stats_total_seconds(const Stats *st)
{
    return st != NULL ? st->total : 0;
}

guint stats_max_in_range(const Stats *st, gint days)
{
    guint max = 0;

    for (gint i = 0; i < days; i++)
        max = MAX(max, stats_seconds_days_ago(st, i));

    return max;
}
