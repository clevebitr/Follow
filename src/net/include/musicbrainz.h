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

#ifndef FOLLOW_MUSICBRAINZ_H
#define FOLLOW_MUSICBRAINZ_H

#include <glib.h>

/* MusicBrainz WS/2 与 Cover Art Archive 客户端。
 * 同步阻塞，调用方须在工作线程中调用；内部限速 1 req/s。 */

typedef struct {
    gchar *mbid;
    gchar *title;
    gchar *artist;
    gchar *album;
    gchar *album_mbid;
    gchar *date;        /* YYYY 或 YYYY-MM-DD */
    gint   score;       /* 0..100 */
} MbCandidate;

typedef struct {
    gchar *mbid;
    gchar *title;
    gchar *artist;
    gchar *album;
    gchar *album_mbid;
    gchar *album_artist;
    gchar *release_date;
    gchar *country;
    gchar *label;
    gchar *catalog_number;
    gchar *barcode;
    gchar *tags;            /* 逗号分隔 */
    gint   duration_ms;
    gint   track_number;
    gint   track_count;
    gchar *cover_url;
} MbRecord;

/* 搜索录音。失败返回 NULL 并写入 *error（新分配，可为 NULL）。 */
GPtrArray *mb_search_recordings(const gchar *title, const gchar *artist,
                                gchar **error);

MbRecord *mb_fetch_recording(const gchar *mbid, gchar **error);

/* 封面 URL（新分配）；无 release MBID 时返回 NULL。 */
gchar *mb_cover_url(const gchar *release_mbid);

void mb_candidate_free(gpointer data);
void mb_record_free(MbRecord *record);

#endif
