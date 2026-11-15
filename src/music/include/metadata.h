/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_METADATA_H
#define FOLLOW_METADATA_H

#include <glib.h>

#include "config.h"

/* 歌曲元数据：MusicBrainz 查询 + 本地 SQLite 缓存。
 *
 * 设计原则：
 *   1. UI 线程只读状态机（metadata_state / metadata_current），永远不阻塞；
 *   2. 联网查询跑在工作线程，完成后 g_idle_add 回 UI 线程合并；
 *   3. 认过的曲目（曲目 key → recording MBID）落 SQLite，之后只读本地库；
 *   4. 第一次遇到一首歌时必须由用户从候选里指认（哪怕只有一个候选）；
 *   5. 封面默认从 Cover Art Archive 下载到封面缓存，路径也写进数据库。
 *
 * 曲目 key：优先 [audioN] page，其次 link，最后 title|artist。
 * 这样重新排序 [audioN] 或改备注不会丢已有映射。 */

typedef enum {
    META_STATE_IDLE = 0,   /* 还没有曲目 */
    META_STATE_LOADING,    /* 正在查本地库 / 等网络结果 */
    META_STATE_READY,      /* 有完整详情 */
    META_STATE_PICK,       /* 等用户从候选里指认 */
    META_STATE_ERROR       /* 查询失败，或没有匹配 */
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
    gchar *cover_path;     /* 本地缓存文件；NULL = 没下载 */
    gchar *source;         /* "musicbrainz" */
    gint64 fetched_at;     /* unix 秒 */
} TrackMeta;

typedef struct {
    gchar *mbid;           /* recording MBID */
    gchar *title;
    gchar *artist;
    gchar *album;
    gchar *album_mbid;
    gchar *date;
    gint   score;
} MetaCandidate;

void metadata_init(void);
void metadata_shutdown(void);

/* 切到这首曲目：命中本地库直接 READY；否则后台搜索。
 * 重复调用同一首是空操作。 */
void metadata_load_track(const AudioTrack *track);

/* PICK 状态下选第 index 个候选并拉详情（结果会落库）。 */
void metadata_pick(gint index);

/* 丢掉当前曲目的本地记录并重新搜索（认错后的「重来」）。 */
void metadata_research(void);

MetaState        metadata_state(void);
const TrackMeta *metadata_current(void);       /* READY 时非 NULL；只读 */
const GPtrArray *metadata_candidates(void);    /* PICK 时 MetaCandidate* 列表 */
gint             metadata_selected(void);
void             metadata_select(gint index);  /* PICK 时移动高亮 */
guint            metadata_version(void);       /* 状态每变一次 +1 */
const gchar     *metadata_error(void);         /* ERROR 时的原因 */
const gchar     *metadata_current_key(void);   /* 当前状态对应的曲目 key */

/* 当前曲目的数据库封面（本地缓存路径 / 原始 URL），没有返回 NULL。 */
const gchar *metadata_cover_path(void);
const gchar *metadata_cover_url(void);

/* 曲目 key（新分配；用于日志 / 调试）。 */
gchar *metadata_track_key(const AudioTrack *track);

/* 释放一个 MetaCandidate（metadb 也用它当 GPtrArray free func）。 */
void meta_candidate_free(gpointer data);

/* 释放一个 TrackMeta（metadb 读出来的记录由调用方释放）。 */
void meta_record_free(TrackMeta *meta);

#endif /* FOLLOW_METADATA_H */
