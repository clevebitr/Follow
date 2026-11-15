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

#ifndef FOLLOW_MUSICBRAINZ_H
#define FOLLOW_MUSICBRAINZ_H

#include <glib.h>

/* MusicBrainz Web Service v2 + Cover Art Archive 的最小客户端。
 *
 *   mb_search_recordings()  按「曲名 + 艺人」搜录音，给首次指认用
 *   mb_fetch_recording()   按 recording MBID 拉完整详情（含专辑 / 厂牌 / 曲序）
 *   mb_cover_url()         专辑封面（Cover Art Archive front-500）
 *
 * 约束：
 *   - MusicBrainz 限速 1 req/s（本模块内部节流，跨线程安全）；
 *   - UA 取 [metadata] / [net] 配置，默认是项目主页；
 *   - 全部同步阻塞，调用方负责放到工作线程里。
 */

/* 搜索候选：一次录音一个。title / artist / album 可能为 NULL。 */
typedef struct {
    gchar *mbid;        /* recording MBID */
    gchar *title;
    gchar *artist;      /* artist credit 拼接结果 */
    gchar *album;       /* 代表专辑（优先正式专辑） */
    gchar *album_mbid;  /* release MBID */
    gchar *date;        /* 该专辑发行日期，YYYY 或 YYYY-MM-DD */
    gint   score;       /* MusicBrainz 相关度 0..100 */
} MbCandidate;

/* 详情：一次录音 + 代表专辑的信息。 */
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
    gchar *tags;            /* "rock, j-pop, ..." */
    gint   duration_ms;
    gint   track_number;
    gint   track_count;
    gchar *cover_url;
} MbRecord;

/* 搜录音。成功返回 GPtrArray<MbCandidate*>（元素用 mb_candidate_free 释放），
 * 失败返回 NULL 并把原因写进 *error（新分配，可 NULL）。 */
GPtrArray *mb_search_recordings(const gchar *title, const gchar *artist,
                                gchar **error);

/* 按 recording MBID 拉详情。失败返回 NULL。 */
MbRecord *mb_fetch_recording(const gchar *mbid, gchar **error);

/* Cover Art Archive 封面地址；release_mbid 为空返回 NULL（新分配）。 */
gchar *mb_cover_url(const gchar *release_mbid);

void mb_candidate_free(gpointer data);
void mb_record_free(MbRecord *record);

#endif /* FOLLOW_MUSICBRAINZ_H */
