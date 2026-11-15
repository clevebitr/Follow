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

#include "metadb.h"

#include <string.h>

#include <sqlite3.h>

#include "config.h"

#define METADB_SCHEMA                                                          \
    "CREATE TABLE IF NOT EXISTS meta ("                                        \
    "  track_key       TEXT PRIMARY KEY,"                                      \
    "  recording_mbid  TEXT,"                                                  \
    "  release_mbid    TEXT,"                                                  \
    "  title           TEXT,"                                                  \
    "  artist          TEXT,"                                                  \
    "  album           TEXT,"                                                  \
    "  album_artist    TEXT,"                                                  \
    "  release_date    TEXT,"                                                  \
    "  country         TEXT,"                                                  \
    "  label           TEXT,"                                                  \
    "  catalog_number  TEXT,"                                                  \
    "  barcode         TEXT,"                                                  \
    "  tags            TEXT,"                                                  \
    "  duration_ms     INTEGER DEFAULT 0,"                                     \
    "  track_number    INTEGER DEFAULT 0,"                                     \
    "  track_count     INTEGER DEFAULT 0,"                                     \
    "  cover_url       TEXT,"                                                  \
    "  cover_path      TEXT,"                                                  \
    "  source          TEXT,"                                                  \
    "  fetched_at      INTEGER DEFAULT 0"                                      \
    ");"                                                                       \
    "CREATE TABLE IF NOT EXISTS candidates ("                                  \
    "  track_key   TEXT NOT NULL,"                                             \
    "  mbid        TEXT NOT NULL,"                                             \
    "  title       TEXT,"                                                      \
    "  artist      TEXT,"                                                      \
    "  album       TEXT,"                                                      \
    "  album_mbid  TEXT,"                                                      \
    "  date        TEXT,"                                                      \
    "  score       INTEGER DEFAULT 0,"                                         \
    "  PRIMARY KEY (track_key, mbid)"                                          \
    ");"                                                                       \
    "CREATE INDEX IF NOT EXISTS meta_album ON meta(release_mbid);"

static sqlite3 *g_db;

static gchar *col_text(sqlite3_stmt *stmt, int index)
{
    const unsigned char *text = sqlite3_column_text(stmt, index);

    return text != NULL ? g_strdup((const gchar *)text) : NULL;
}

static gint col_int(sqlite3_stmt *stmt, int index)
{
    return sqlite3_column_int(stmt, index);
}

gboolean metadb_open(void)
{
    g_autofree gchar *path = config_metadata_db_path();
    g_autofree gchar *dir  = g_path_get_dirname(path);
    gchar            *err  = NULL;

    if (g_db != NULL)
        return TRUE;

    if (g_mkdir_with_parents(dir, 0700) != 0)
        g_warning("metadb: cannot create %s", dir);

    if (sqlite3_open_v2(path, &g_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                        SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK) {
        g_warning("metadb: cannot open %s: %s", path,
                  g_db != NULL ? sqlite3_errmsg(g_db) : "unknown");

        if (g_db != NULL) {
            sqlite3_close(g_db);
            g_db = NULL;
        }
        return FALSE;
    }

    sqlite3_busy_timeout(g_db, 3000);

    if (sqlite3_exec(g_db, METADB_SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        g_warning("metadb: schema failed: %s", err != NULL ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(g_db);
        g_db = NULL;
        return FALSE;
    }

    g_message("metadb: %s", path);
    return TRUE;
}

void metadb_close(void)
{
    if (g_db != NULL) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

static TrackMeta *record_from_stmt(sqlite3_stmt *stmt)
{
    TrackMeta *meta = g_new0(TrackMeta, 1);

    meta->track_key      = col_text(stmt, 0);
    meta->recording_mbid = col_text(stmt, 1);
    meta->release_mbid   = col_text(stmt, 2);
    meta->title          = col_text(stmt, 3);
    meta->artist         = col_text(stmt, 4);
    meta->album          = col_text(stmt, 5);
    meta->album_artist   = col_text(stmt, 6);
    meta->release_date   = col_text(stmt, 7);
    meta->country        = col_text(stmt, 8);
    meta->label          = col_text(stmt, 9);
    meta->catalog_number = col_text(stmt, 10);
    meta->barcode        = col_text(stmt, 11);
    meta->tags           = col_text(stmt, 12);
    meta->duration_ms    = col_int(stmt, 13);
    meta->track_number   = col_int(stmt, 14);
    meta->track_count    = col_int(stmt, 15);
    meta->cover_url      = col_text(stmt, 16);
    meta->cover_path     = col_text(stmt, 17);
    meta->source         = col_text(stmt, 18);
    meta->fetched_at     = sqlite3_column_int64(stmt, 19);

    return meta;
}

gboolean metadb_get(const gchar *key, TrackMeta **out)
{
    static const char *sql =
        "SELECT track_key, recording_mbid, release_mbid, title, artist, album,"
        "       album_artist, release_date, country, label, catalog_number,"
        "       barcode, tags, duration_ms, track_number, track_count,"
        "       cover_url, cover_path, source, fetched_at"
        "  FROM meta WHERE track_key = ?1";
    sqlite3_stmt *stmt = NULL;
    gboolean      found = FALSE;

    if (out != NULL)
        *out = NULL;

    if (g_db == NULL || key == NULL || out == NULL)
        return FALSE;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return FALSE;

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out  = record_from_stmt(stmt);
        found = TRUE;
    }

    sqlite3_finalize(stmt);
    return found;
}

gboolean metadb_put(const TrackMeta *meta)
{
    static const char *sql =
        "INSERT OR REPLACE INTO meta ("
        " track_key, recording_mbid, release_mbid, title, artist, album,"
        " album_artist, release_date, country, label, catalog_number, barcode,"
        " tags, duration_ms, track_number, track_count, cover_url, cover_path,"
        " source, fetched_at"
        ") VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,"
        "          ?17,?18,?19,?20)";
    sqlite3_stmt *stmt = NULL;
    gboolean      ok;

    if (g_db == NULL || meta == NULL || meta->track_key == NULL)
        return FALSE;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return FALSE;

    sqlite3_bind_text(stmt, 1, meta->track_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, meta->recording_mbid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, meta->release_mbid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, meta->title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, meta->artist, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, meta->album, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, meta->album_artist, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, meta->release_date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, meta->country, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, meta->label, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, meta->catalog_number, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, meta->barcode, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, meta->tags, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, meta->duration_ms);
    sqlite3_bind_int(stmt, 15, meta->track_number);
    sqlite3_bind_int(stmt, 16, meta->track_count);
    sqlite3_bind_text(stmt, 17, meta->cover_url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, meta->cover_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 19, meta->source, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 20, meta->fetched_at);

    ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok)
        g_warning("metadb: put failed: %s", sqlite3_errmsg(g_db));

    sqlite3_finalize(stmt);
    return ok;
}

void metadb_remove(const gchar *key)
{
    sqlite3_stmt *stmt = NULL;

    if (g_db == NULL || key == NULL)
        return;

    if (sqlite3_prepare_v2(g_db, "DELETE FROM meta WHERE track_key = ?1",
                           -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
}

GPtrArray *metadb_get_candidates(const gchar *key)
{
    static const char *sql =
        "SELECT mbid, title, artist, album, album_mbid, date, score"
        "  FROM candidates WHERE track_key = ?1 ORDER BY score DESC";
    sqlite3_stmt *stmt = NULL;
    GPtrArray    *out;

    if (g_db == NULL || key == NULL)
        return NULL;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    out = g_ptr_array_new_with_free_func(meta_candidate_free);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MetaCandidate *cand = g_new0(MetaCandidate, 1);

        cand->mbid       = col_text(stmt, 0);
        cand->title      = col_text(stmt, 1);
        cand->artist     = col_text(stmt, 2);
        cand->album      = col_text(stmt, 3);
        cand->album_mbid = col_text(stmt, 4);
        cand->date       = col_text(stmt, 5);
        cand->score      = col_int(stmt, 6);

        g_ptr_array_add(out, cand);
    }

    sqlite3_finalize(stmt);

    if (out->len == 0) {
        g_ptr_array_unref(out);
        return NULL;
    }

    return out;
}

gboolean metadb_put_candidates(const gchar *key, const GPtrArray *candidates)
{
    sqlite3_stmt *stmt = NULL;

    if (g_db == NULL || key == NULL || candidates == NULL)
        return FALSE;

    metadb_clear_candidates(key);

    if (sqlite3_prepare_v2(g_db,
                           "INSERT OR REPLACE INTO candidates"
                           " (track_key, mbid, title, artist, album,"
                           "  album_mbid, date, score)"
                           " VALUES (?1,?2,?3,?4,?5,?6,?7,?8)",
                           -1, &stmt, NULL) != SQLITE_OK)
        return FALSE;

    for (guint i = 0; i < candidates->len; i++) {
        const MetaCandidate *cand = g_ptr_array_index(candidates, i);

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, cand->mbid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, cand->title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, cand->artist, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, cand->album, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, cand->album_mbid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, cand->date, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, cand->score);

        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    return TRUE;
}

void metadb_clear_candidates(const gchar *key)
{
    sqlite3_stmt *stmt = NULL;

    if (g_db == NULL || key == NULL)
        return;

    if (sqlite3_prepare_v2(g_db,
                           "DELETE FROM candidates WHERE track_key = ?1",
                           -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
}
