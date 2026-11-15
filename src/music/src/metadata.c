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

#include "metadata.h"

#include <string.h>

#include <glib/gstdio.h>

#include "cache.h"
#include "config.h"
#include "http.h"
#include "metadb.h"
#include "musicbrainz.h"

/* ---------------------------------------------------------------------------
 *  元数据状态机
 *
 *   UI 线程                     工作线程
 *   ────────────────────────    ────────────────────────────
 *   metadata_load_track()
 *     ├─ SQLite 命中 → READY
 *     ├─ 候选缓存   → PICK（离线也能指认）
 *     └─ 网络搜索   ─────────►   mb_search_recordings()
 *                                   │ g_idle_add
 *   PICK → 用户 Enter            ◄──┘
 *     └─ 拉详情     ─────────►   mb_fetch_recording() + 封面下载
 *                                   │ g_idle_add
 *   READY（写 SQLite）           ◄──┘
 *
 * 所有 M 里的字段只在 UI 线程读写；工作线程只碰自己的 MetaJob。
 * ------------------------------------------------------------------------- */

typedef enum {
    META_JOB_SEARCH = 0,
    META_JOB_FETCH,
    META_JOB_COVER
} MetaJobKind;

typedef struct {
    guint       gen;
    MetaJobKind kind;
    gchar      *key;
    gchar      *title;
    gchar      *artist;
    gchar      *mbid;
    gchar      *url;          /* COVER：封面地址 */

    GPtrArray  *candidates;   /* SEARCH 结果 */
    TrackMeta  *meta;         /* FETCH 结果 */
    gchar      *cover_path;   /* COVER 结果 */
    gchar      *error;
} MetaJob;

/* 完成的工作线程结果队列。
 *
 * 不用 g_idle_add：详情页打开后 GTK 会留下一个长期 ready 的默认优先级
 * source，DEFAULT_IDLE 优先级的回调会被饿死（定时器还在跑，idle 不跑）。
 * 这里改成「工作线程入队 + 60ms 定时器在 UI 线程取」，只依赖定时器。 */
static GMutex  g_done_mutex;
static GQueue  g_done_queue = G_QUEUE_INIT;
static guint   g_poll_id;

static struct {
    MetaState   state;
    gchar      *key;
    gchar      *title;
    gchar      *artist;
    TrackMeta  *meta;
    GPtrArray  *candidates;
    gint        selected;
    gchar      *error;
    guint       version;
    guint       gen;
    gboolean    want_cover;
    gboolean    cover_loading;
    gboolean    shutting_down;
    gboolean    initialized;
} M = { .state = META_STATE_IDLE, .selected = 0 };

static gboolean meta_poll(gpointer data);
static void     meta_job_free(MetaJob *job);

/* ------------------------------------------------------------ 生命周期 --- */

void metadata_init(void)
{
    if (M.initialized)
        return;

    metadb_open();
    M.initialized   = TRUE;
    M.shutting_down = FALSE;
    M.state         = META_STATE_IDLE;

    if (g_poll_id == 0)
        g_poll_id = g_timeout_add(60, meta_poll, NULL);
}

void metadata_shutdown(void)
{
    if (!M.initialized)
        return;

    M.gen++;                    /* 让还在飞的工作线程结果作废 */
    M.shutting_down = TRUE;

    g_clear_pointer(&M.key, g_free);
    g_clear_pointer(&M.title, g_free);
    g_clear_pointer(&M.artist, g_free);
    g_clear_pointer(&M.error, g_free);
    g_clear_pointer(&M.meta, meta_record_free);

    if (M.candidates != NULL)
        g_ptr_array_unref(M.candidates);
    M.candidates = NULL;

    if (g_poll_id != 0) {
        g_source_remove(g_poll_id);
        g_poll_id = 0;
    }

    /* 队列里没处理的结果直接释放 */
    g_mutex_lock(&g_done_mutex);
    for (GList *it = g_done_queue.head; it != NULL; it = it->next)
        meta_job_free(it->data);
    g_queue_clear(&g_done_queue);
    g_mutex_unlock(&g_done_mutex);

    metadb_close();
    M.initialized = FALSE;
    M.state       = META_STATE_IDLE;
}

gchar *metadata_track_key(const AudioTrack *track)
{
    if (track == NULL)
        return NULL;

    if (track->page != NULL && *track->page != '\0')
        return g_strdup(track->page);

    if (track->link != NULL && *track->link != '\0')
        return g_strdup(track->link);

    {
        g_autofree gchar *title = config_track_title(track);

        return g_strdup_printf("%s|%s",
                               title != NULL ? title : "unknown",
                               track->artist != NULL ? track->artist : "");
    }
}

void meta_candidate_free(gpointer data)
{
    MetaCandidate *cand = data;

    if (cand == NULL)
        return;

    g_free(cand->mbid);
    g_free(cand->title);
    g_free(cand->artist);
    g_free(cand->album);
    g_free(cand->album_mbid);
    g_free(cand->date);
    g_free(cand);
}

void meta_record_free(TrackMeta *meta)
{
    if (meta == NULL)
        return;

    g_free(meta->track_key);
    g_free(meta->recording_mbid);
    g_free(meta->release_mbid);
    g_free(meta->title);
    g_free(meta->artist);
    g_free(meta->album);
    g_free(meta->album_artist);
    g_free(meta->release_date);
    g_free(meta->country);
    g_free(meta->label);
    g_free(meta->catalog_number);
    g_free(meta->barcode);
    g_free(meta->tags);
    g_free(meta->cover_url);
    g_free(meta->cover_path);
    g_free(meta->source);
    g_free(meta);
}

/* --------------------------------------------------------------- 查询 --- */

MetaState metadata_state(void)
{
    return M.state;
}

const TrackMeta *metadata_current(void)
{
    return M.meta;
}

const GPtrArray *metadata_candidates(void)
{
    return M.candidates;
}

gint metadata_selected(void)
{
    return M.selected;
}

guint metadata_version(void)
{
    return M.version;
}

const gchar *metadata_error(void)
{
    return M.error;
}

const gchar *metadata_current_key(void)
{
    return M.key;
}

const gchar *metadata_cover_path(void)
{
    return M.meta != NULL ? M.meta->cover_path : NULL;
}

const gchar *metadata_cover_url(void)
{
    return M.meta != NULL ? M.meta->cover_url : NULL;
}

/* --------------------------------------------------------------- 状态 --- */

static void state_set_error(const gchar *message)
{
    g_free(M.error);
    M.error = g_strdup(message != NULL && *message != '\0'
                       ? message : "元数据查询失败");
    M.state = META_STATE_ERROR;
    M.version++;
}

static void state_bump(MetaState state)
{
    M.state = state;
    M.version++;
}

static void clear_candidates(void)
{
    if (M.candidates != NULL)
        g_ptr_array_unref(M.candidates);
    M.candidates = NULL;
    M.selected   = 0;
}

static void clear_meta(void)
{
    g_clear_pointer(&M.meta, meta_record_free);
}

/* --------------------------------------------------------- 工作线程 ------ */

static void meta_job_free(MetaJob *job)
{
    if (job == NULL)
        return;

    g_free(job->key);
    g_free(job->title);
    g_free(job->artist);
    g_free(job->mbid);
    g_free(job->url);
    g_free(job->cover_path);
    g_free(job->error);

    if (job->candidates != NULL)
        g_ptr_array_unref(job->candidates);

    meta_record_free(job->meta);
    g_free(job);
}

static GPtrArray *convert_candidates(GPtrArray *mb_list)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(meta_candidate_free);

    for (guint i = 0; mb_list != NULL && i < mb_list->len; i++) {
        MbCandidate   *src = g_ptr_array_index(mb_list, i);
        MetaCandidate *dst = g_new0(MetaCandidate, 1);

        dst->mbid       = g_strdup(src->mbid);
        dst->title      = g_strdup(src->title);
        dst->artist     = g_strdup(src->artist);
        dst->album      = g_strdup(src->album);
        dst->album_mbid = g_strdup(src->album_mbid);
        dst->date       = g_strdup(src->date);
        dst->score      = src->score;

        g_ptr_array_add(out, dst);
    }

    return out;
}

/* 封面下载：<cache>/metacover-<md5(url)>.img，命中就不重复拉。 */
static gchar *cover_download(const gchar *url)
{
    g_autofree gchar *path = NULL;
    HttpResponse       resp = { 0 };
    gboolean           ok   = FALSE;

    if (url == NULL || *url == '\0')
        return NULL;

    path = follow_cache_file("metacover", url, "img");

    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        GStatBuf st;

        if (g_stat(path, &st) == 0 && st.st_size > 0)
            return g_steal_pointer(&path);
    }

    if (http_get(url, &resp, 30) == 0 && resp.status == 200 && resp.length > 0)
        ok = g_file_set_contents(path, resp.body, (gssize)resp.length, NULL);

    http_response_free(&resp);

    if (!ok) {
        g_warning("metadata: cover download failed: %s", url);
        return NULL;
    }

    cache_prune_prefix("metacover", config_cover_max_bytes());
    return g_steal_pointer(&path);
}

static TrackMeta *record_from_mb(const gchar *key, const MbRecord *rec,
                                 const gchar *cover_path)
{
    TrackMeta *meta = g_new0(TrackMeta, 1);

    meta->track_key      = g_strdup(key);
    meta->recording_mbid = g_strdup(rec->mbid);
    meta->release_mbid   = g_strdup(rec->album_mbid);
    meta->title          = g_strdup(rec->title);
    meta->artist         = g_strdup(rec->artist);
    meta->album          = g_strdup(rec->album);
    meta->album_artist   = g_strdup(rec->album_artist);
    meta->release_date   = g_strdup(rec->release_date);
    meta->country        = g_strdup(rec->country);
    meta->label          = g_strdup(rec->label);
    meta->catalog_number = g_strdup(rec->catalog_number);
    meta->barcode        = g_strdup(rec->barcode);
    meta->tags           = g_strdup(rec->tags);
    meta->duration_ms    = rec->duration_ms;
    meta->track_number   = rec->track_number;
    meta->track_count    = rec->track_count;
    meta->cover_url      = g_strdup(rec->cover_url);
    meta->cover_path     = g_strdup(cover_path);
    meta->source         = g_strdup("musicbrainz");
    meta->fetched_at     = g_get_real_time() / G_USEC_PER_SEC;

    return meta;
}

static gboolean meta_job_done(gpointer data);

static gpointer meta_worker(gpointer data)
{
    MetaJob *job = data;

    g_debug("metadata: worker start (kind=%d gen=%u)",
            (int)job->kind, job->gen);

    if (job->kind == META_JOB_SEARCH) {
        GPtrArray *list = mb_search_recordings(job->title, job->artist,
                                               &job->error);

        job->candidates = convert_candidates(list);

        if (list != NULL)
            g_ptr_array_unref(list);
    } else if (job->kind == META_JOB_FETCH) {
        MbRecord *record = mb_fetch_recording(job->mbid, &job->error);

        if (record != NULL) {
            job->meta = record_from_mb(job->key, record, NULL);
            mb_record_free(record);
        }
    } else {
        /* 封面单独下：CAA 会重定向到 archive.org，慢的时候十几秒，
         * 不能挡住详情页显示。 */
        job->cover_path = cover_download(job->url);
    }

    g_debug("metadata: worker done (kind=%d gen=%u err=%s)",
            (int)job->kind, job->gen,
            job->error != NULL ? job->error : "-");

    g_mutex_lock(&g_done_mutex);

    if (M.shutting_down) {
        g_mutex_unlock(&g_done_mutex);
        meta_job_free(job);
        return NULL;
    }

    g_queue_push_tail(&g_done_queue, job);
    g_mutex_unlock(&g_done_mutex);

    return NULL;
}

/* UI 线程：把已经完成的结果逐个合并进状态机 */
static gboolean meta_poll(gpointer data)
{
    MetaJob *job = NULL;

    (void)data;

    for (;;) {
        g_mutex_lock(&g_done_mutex);
        job = g_queue_pop_head(&g_done_queue);
        g_mutex_unlock(&g_done_mutex);

        if (job == NULL)
            break;

        meta_job_done(job);
    }

    return G_SOURCE_CONTINUE;
}

static void meta_job_launch(MetaJob *job, const gchar *name)
{
    job->gen = M.gen;

    if (job->gen == 0)
        job->gen = ++M.gen;

    g_thread_unref(g_thread_new(name, meta_worker, job));
}

static void start_search(void)
{
    MetaJob *job;

    if (M.key == NULL)
        return;

    g_clear_pointer(&M.error, g_free);

    job         = g_new0(MetaJob, 1);
    job->kind   = META_JOB_SEARCH;
    job->key    = g_strdup(M.key);
    job->title  = g_strdup(M.title);
    job->artist = g_strdup(M.artist);

    g_message("metadata: searching \"%s\" / \"%s\"",
              M.title != NULL ? M.title : "",
              M.artist != NULL ? M.artist : "");

    state_bump(META_STATE_LOADING);
    meta_job_launch(job, "follow-meta-search");
}

static void start_fetch(gint index)
{
    const MetaCandidate *cand;
    MetaJob             *job;

    if (M.candidates == NULL || index < 0 ||
        (guint)index >= M.candidates->len)
        return;

    cand = g_ptr_array_index(M.candidates, (guint)index);

    M.selected = index;
    g_clear_pointer(&M.error, g_free);

    job              = g_new0(MetaJob, 1);
    job->kind        = META_JOB_FETCH;
    job->key         = g_strdup(M.key);
    job->mbid        = g_strdup(cand->mbid);
    job->title       = g_strdup(M.title);
    job->artist      = g_strdup(M.artist);

    g_message("metadata: fetching %s", job->mbid);

    state_bump(META_STATE_LOADING);
    meta_job_launch(job, "follow-meta-fetch");
}

static void start_cover(const gchar *url)
{
    MetaJob *job;

    if (url == NULL || *url == '\0' || !M.want_cover || M.cover_loading)
        return;

    M.cover_loading = TRUE;

    job            = g_new0(MetaJob, 1);
    job->kind      = META_JOB_COVER;
    job->key       = g_strdup(M.key);
    job->url       = g_strdup(url);

    g_message("metadata: fetching cover %s", url);
    meta_job_launch(job, "follow-meta-cover");
}

static gboolean meta_job_done(gpointer data)
{
    MetaJob *job = data;

    g_debug("metadata: job done (kind=%d gen=%u/%u)",
            (int)job->kind, job->gen, M.gen);

    /* 曲目切走了 / 重新搜索了：结果直接丢掉 */
    if (job->gen != M.gen || M.key == NULL ||
        g_strcmp0(job->key, M.key) != 0) {
        meta_job_free(job);
        return G_SOURCE_REMOVE;
    }

    if (job->kind == META_JOB_SEARCH) {
        clear_candidates();

        if (job->candidates == NULL || job->candidates->len == 0) {
            g_message("metadata: search failed: %s",
                      job->error != NULL ? job->error : "no match");
            state_set_error(job->error != NULL ? job->error
                                               : "没有找到匹配的录音");
        } else {
            M.candidates     = job->candidates;
            job->candidates  = NULL;

            metadb_put_candidates(M.key, M.candidates);

            /* 第一次识别必须由用户指认：即使只有一个候选也让人看一眼再 Enter，
             * 避免搜索条件太宽时把错的录音悄悄存下来。 */
            M.selected = 0;
            state_bump(META_STATE_PICK);
            g_message("metadata: %u candidate(s), waiting for user",
                      M.candidates->len);
        }
    } else if (job->kind == META_JOB_FETCH) {
        if (job->meta == NULL) {
            g_message("metadata: fetch failed: %s",
                      job->error != NULL ? job->error : "unknown");
            state_set_error(job->error != NULL ? job->error : "获取详情失败");
        } else {
            clear_meta();
            M.meta    = job->meta;
            job->meta = NULL;

            metadb_put(M.meta);
            state_bump(META_STATE_READY);

            g_message("metadata: %s → %s / %s (%s)",
                      M.meta->title != NULL ? M.meta->title : "-",
                      M.meta->artist != NULL ? M.meta->artist : "-",
                      M.meta->album != NULL ? M.meta->album : "-",
                      M.meta->recording_mbid != NULL ? M.meta->recording_mbid : "-");

            /* 详情先上屏，封面在后台继续下 */
            start_cover(M.meta->cover_url);
        }
    } else {
        M.cover_loading = FALSE;

        if (job->cover_path != NULL && M.meta != NULL) {
            g_free(M.meta->cover_path);
            M.meta->cover_path = g_strdup(job->cover_path);
            metadb_put(M.meta);
            M.version++;

            g_message("metadata: cover ready %s", job->cover_path);
        } else {
            g_debug("metadata: cover unavailable");
        }
    }

    meta_job_free(job);
    return G_SOURCE_REMOVE;
}

/* --------------------------------------------------------------- 入口 --- */

void metadata_load_track(const AudioTrack *track)
{
    g_autofree gchar *key = metadata_track_key(track);

    if (!M.initialized)
        metadata_init();

    if (key == NULL) {
        M.gen++;
        g_clear_pointer(&M.key, g_free);
        g_clear_pointer(&M.title, g_free);
        g_clear_pointer(&M.artist, g_free);
        g_clear_pointer(&M.error, g_free);
        clear_candidates();
        clear_meta();
        state_bump(META_STATE_IDLE);
        return;
    }

    /* 同一首歌：已经出结果 / 正在查 / 上次失败，都不用每个 tick 重来。
     * 重新查询走 metadata_research()。 */
    if (g_strcmp0(key, M.key) == 0 && M.state != META_STATE_IDLE)
        return;

    M.gen++;

    g_free(M.key);
    g_free(M.title);
    g_free(M.artist);
    g_clear_pointer(&M.error, g_free);
    clear_candidates();
    clear_meta();

    M.key    = g_strdup(key);
    M.artist = g_strdup(track->artist);

    {
        g_autofree gchar *title = config_track_title(track);

        M.title = g_strdup(title != NULL ? title : key);
    }

    M.selected   = 0;
    M.want_cover = config_metadata_cover() &&
                   (track->pic == NULL || *track->pic == '\0');

    /* 1) 本地库：之后打开详情页不再发任何请求 */
    if (metadb_get(M.key, &M.meta)) {
        g_message("metadata: cache hit %s", M.key);
        state_bump(META_STATE_READY);

        /* 上次可能因为配置里有 pic 没下封面；现在需要了就补一次 */
        if (M.want_cover && M.meta->cover_url != NULL &&
            (M.meta->cover_path == NULL ||
             !g_file_test(M.meta->cover_path, G_FILE_TEST_EXISTS)))
            start_cover(M.meta->cover_url);

        return;
    }

    /* 2) 上次搜索的候选还在：离线也能指认 */
    M.candidates = metadb_get_candidates(M.key);

    if (M.candidates != NULL) {
        state_bump(META_STATE_PICK);
        return;
    }

    /* 3) 联网搜索（用户关掉了就只报错，不发请求） */
    if (!config_metadata_enabled()) {
        state_set_error("元数据查询已关闭（[metadata] enabled = false）");
        return;
    }

    start_search();
}

void metadata_pick(gint index)
{
    if (M.state != META_STATE_PICK)
        return;

    start_fetch(index);
}

void metadata_select(gint index)
{
    gint n;

    if (M.state != META_STATE_PICK || M.candidates == NULL)
        return;

    n = (gint)M.candidates->len;
    if (n <= 0)
        return;

    if (index < 0)
        index = 0;
    if (index >= n)
        index = n - 1;

    if (index != M.selected) {
        M.selected = index;
        M.version++;
    }
}

void metadata_research(void)
{
    if (!M.initialized || M.key == NULL)
        return;

    M.gen++;

    clear_candidates();
    clear_meta();
    metadb_remove(M.key);
    metadb_clear_candidates(M.key);

    if (!config_metadata_enabled()) {
        state_set_error("元数据查询已关闭（[metadata] enabled = false）");
        return;
    }

    start_search();
}
