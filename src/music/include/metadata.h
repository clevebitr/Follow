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

#ifndef FOLLOW_METADATA_H
#define FOLLOW_METADATA_H

#include <glib.h>

#include "config.h"

/* 歌曲元数据：MusicBrainz 查询 + 本地 SQLite 缓存。
 * 状态机仅在 UI 线程读写；网络请求在工作线程执行，结果由定时器合并。
 * 曲目 key 依次取 page、link、title|artist。 */

typedef enum {
    META_STATE_IDLE = 0,
    META_STATE_LOADING,
    META_STATE_READY,
    META_STATE_PICK,
    META_STATE_ERROR
} MetaState;

typedef struct {
    gchar *track_key;
    gchar *recording_mbid;
    gchar *release_mbid;
    gchar *title;
    gchar *artist;
    gchar *album;
    gchar *album_artist;
    gchar *release_date;
    gchar *country;
    gchar *label;
    gchar *catalog_number;
    gchar *barcode;
    gchar *tags;
    gint   duration_ms;
    gint   track_number;
    gint   track_count;
    gchar *cover_url;
    gchar *cover_path;     /* NULL = 未下载 */
    gchar *source;
    gint64 fetched_at;
} TrackMeta;

typedef struct {
    gchar *mbid;
    gchar *title;
    gchar *artist;
    gchar *album;
    gchar *album_mbid;
    gchar *date;
    gint   score;
} MetaCandidate;

void metadata_init(void);
void metadata_shutdown(void);

/* 加载曲目元数据；本地命中时同步进入 READY，否则后台搜索。 */
void metadata_load_track(const AudioTrack *track);

/* 选择候选并拉取详情，结果写入本地库。 */
void metadata_pick(gint index);

/* 清除本地记录并重新搜索。 */
void metadata_research(void);

MetaState        metadata_state(void);
const TrackMeta *metadata_current(void);       /* 以下返回值均为内部只读数据，调用方不得释放。 */
const GPtrArray *metadata_candidates(void);
gint             metadata_selected(void);
void             metadata_select(gint index);
guint            metadata_version(void);
const gchar     *metadata_error(void);
const gchar     *metadata_current_key(void);

/* 封面本地路径 / 原始 URL；无则 NULL。 */
const gchar *metadata_cover_path(void);
const gchar *metadata_cover_url(void);

/* 返回新分配的曲目 key。 */
gchar *metadata_track_key(const AudioTrack *track);

/* 释放 MetaCandidate。 */
void meta_candidate_free(gpointer data);

/* 释放 TrackMeta。 */
void meta_record_free(TrackMeta *meta);

#endif
