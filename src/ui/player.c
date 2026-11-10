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

#include "player.h"

#include <gtk/gtk.h>

#include "cover.h"
#include "lyricview.h"
#include "qrcard.h"
#include "source.h"
#include "stage.h"
#include "theme.h"
#include "ui.h"

/* 网络地址解析（后台线程） */

typedef struct {
    guint       gen;
    MediaSource src;
    gchar      *uri;
    gchar      *error;
} ResolveJob;

static gpointer resolve_thread(gpointer data);

static void resolve_start(const gchar *uri, guint gen)
{
    ResolveJob *job = g_new0(ResolveJob, 1);

    job->gen = gen;
    job->uri = g_strdup(uri);

    g_thread_unref(g_thread_new("follow-resolve", resolve_thread, job));
}

static void set_resolved_meta(const gchar *title, const gchar *artist)
{
    g_free(app.resolved_title);
    g_free(app.resolved_artist);
    app.resolved_title  = g_strdup(title);
    app.resolved_artist = g_strdup(artist);
    ui_request_redraw();
}

/* 解析失败的原因：显示到歌词那行，不然用户只看到"没反应" */
static void set_source_error(const gchar *message)
{
    g_free(app.source_error);
    app.source_error = (message != NULL && *message != '\0') ? g_strdup(message) : NULL;
}

static gboolean resolve_done(gpointer data)
{
    ResolveJob *job = data;

    if (job->gen == app.resolve_gen) {
        app.source_pending = FALSE;

        if (job->src.url != NULL) {
            if (media_play(&app.media, job->src.url, job->src.headers, job->src.ranged) == 0) {
                const AudioTrack *track = config_track_at(config_get(), (gsize)app.track);
                gboolean has_title  = track != NULL && track->title != NULL && *track->title != '\0';
                gboolean has_artist = track != NULL && track->artist != NULL && *track->artist != '\0';

                app.shown_second = -1;
                app.source_cached = job->src.from_cache;
                set_source_error(NULL);

                /* ini 写了就用 ini 的，只有缺的字段才用解析结果补 */
                if (!has_title || !has_artist)
                    set_resolved_meta(has_title ? NULL : job->src.title,
                                      has_artist ? NULL : job->src.artist);

                player_update_window_title();
                ui_request_redraw();
            } else {
                app.source_cached = FALSE;
                set_source_error("解码器打不开这个地址（详见日志）");
                ui_request_redraw();
            }
        } else {
            app.source_cached = FALSE;
            g_warning("play: %s", job->error != NULL ? job->error : "解析失败");
            set_source_error(job->error != NULL ? job->error : "解析失败");
            ui_request_redraw();
        }
    }

    source_clear(&job->src);
    g_free(job->uri);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer resolve_thread(gpointer data)
{
    ResolveJob *job = data;

    if (source_resolve(job->uri, &job->src, &job->error) != 0 && job->error == NULL)
        job->error = g_strdup("解析失败");

    g_idle_add(resolve_done, job);
    return NULL;
}

/* 曲目信息 */

/* 优先级：ini 里的 title/artist > 网络解析 > 文件名。
 * 还没开始播放时（app.track < 0）先显示列表里第一首的资料。 */
gchar *player_label(void)
{
    const AudioTrack *track = config_track_at(config_get(),
                                              app.track >= 0 ? (gsize)app.track : 0);
    const gchar      *artist = NULL;
    const gchar      *title  = NULL;
    g_autofree gchar *fallback = NULL;

    if (track != NULL && track->artist != NULL && *track->artist != '\0')
        artist = track->artist;
    else if (app.resolved_artist != NULL && *app.resolved_artist != '\0')
        artist = app.resolved_artist;

    if (track != NULL && track->title != NULL && *track->title != '\0')
        title = track->title;
    else if (app.resolved_title != NULL && *app.resolved_title != '\0')
        title = app.resolved_title;

    if (title == NULL && track != NULL) {
        fallback = config_track_title(track);
        title    = fallback;
    }

    if (artist != NULL && title != NULL && *title != '\0')
        return g_strdup_printf("%s - %s", artist, title);
    if (title != NULL && *title != '\0')
        return g_strdup(title);

    return artist != NULL ? g_strdup(artist) : NULL;
}

void player_update_window_title(void)
{
    g_autofree gchar *label = player_label();
    g_autofree gchar *title = (label != NULL && *label != '\0')
        ? g_strdup_printf("%s  —  Follow", label) : NULL;

    gtk_window_set_title(GTK_WINDOW(app.window), title != NULL ? title : "Follow");
}

/* 切歌 */

static void history_push(gint index)
{
    if (app.history_len < (gint)G_N_ELEMENTS(app.history))
        app.history[app.history_len++] = index;
}

gboolean player_play_index(gint index)
{
    const APPconfig   *cfg   = config_get();
    const AudioTrack  *track = config_track_at(cfg, (gsize)MAX(index, 0));
    g_autofree gchar  *local = NULL;

    if (index < 0 || track == NULL)
        return FALSE;

    /* 先把现在的画面拍下来：切歌后它整块往左退场 */
    stage_snapshot();

    /* 每次切歌都作废还在路上的解析请求 */
    media_begin_track(&app.media);

    app.track        = index;
    app.shown_second = -1;
    history_push(index);

    app.source_cached  = FALSE;
    app.source_retried = FALSE;
    app.source_pending = FALSE;

    cover_set(track);
    lyricview_load(track);      /* 顺便把这首歌的歌词偏移重置成 ini 里的值 */
    qrcard_update(track);
    set_resolved_meta(NULL, NULL);
    set_source_error(NULL);
    player_update_window_title();
    ui_request_redraw();

    if (track->link_remote) {
        g_message("play: track[%d] %s -> resolving %s", index, track->id, track->link);

        app.source_pending = TRUE;
        resolve_start(track->link, ++app.resolve_gen);
        return TRUE;
    }

    ++app.resolve_gen;      /* 本地曲目：同样作废上一首还没回来的解析 */

    local = config_to_local_path(track->link);

    if (local == NULL || !g_file_test(local, G_FILE_TEST_EXISTS)) {
        g_warning("track %s: audio file not found: %s (cwd = %s)",
                  track->id, track->link, g_get_current_dir());
        return FALSE;
    }

    if (media_play(&app.media, local, NULL, FALSE) != 0) {
        g_warning("track %s: cannot play %s", track->id, local);
        return FALSE;
    }

    g_message("play: track[%d] %s -> %s", index, track->id, local);
    return TRUE;
}

/* 随机挑一个和当前不同的曲目 */
static gboolean play_random(void)
{
    gint n = (gint)config_get()->n_tracks;

    if (n <= 0)
        return FALSE;

    if (n == 1)
        return player_play_index(0);

    for (gint attempt = 0; attempt < n * 3; attempt++) {
        gint idx = g_random_int_range(0, n);

        if (idx != app.track && player_play_index(idx))
            return TRUE;
    }

    return FALSE;
}

void player_play_neighbour(gint dir)
{
    gint n     = (gint)config_get()->n_tracks;
    gint start = app.track;

    if (n <= 0) {
        g_warning("playlist: no [audioN] section in src/config.ini");
        return;
    }

    /* 随机模式 */
    if (app.shuffle) {
        if (dir < 0 && app.history_len > 1) {
            app.history_len--;                 /* 栈顶是当前曲目，先弹掉 */

            for (gint i = app.history_len - 1; i >= 0; i--) {
                if (player_play_index(app.history[i]))
                    return;                    /* play_index 会把它重新压栈 */
            }
        }

        if (play_random())
            return;

        g_warning("shuffle: no playable track, falling back to sequential");
    }

    for (gint k = 1; k <= n; k++) {
        gint idx = (start + k * dir) % n;

        if (idx < 0)
            idx += n;

        if (player_play_index(idx))
            return;
    }

    g_warning("playlist: no playable track found");
}

gboolean player_play_first(void)
{
    const APPconfig *cfg = config_get();

    if (cfg->n_tracks == 0) {
        g_warning("playlist: no [audioN] section in src/config.ini");
        return FALSE;
    }

    for (gsize i = 0; i < cfg->n_tracks; i++) {
        if (player_play_index((gint)i))
            return TRUE;
    }

    g_warning("playlist: none of the %" G_GSIZE_FORMAT " track(s) is playable",
              cfg->n_tracks);
    return FALSE;
}

/* 传输控制 */

void player_toggle(void)
{
    if (!app.media.ready) {
        g_warning("media engine not ready");
        return;
    }

    if (media_is_playing(&app.media)) {
        media_pause(&app.media);
    } else if (media_is_paused(&app.media) ||
               (app.media.has_stream && app.track >= 0)) {
        media_continue(&app.media);
    } else {
        player_play_first();
    }

    ui_request_redraw();
}

void player_restart(void)
{
    if (app.track >= 0)
        player_play_index(app.track);
    else
        player_play_first();
}

void player_stop(void)
{
    media_stop(&app.media);
    set_source_error(NULL);
    ui_request_redraw();
}

void player_seek_by(double delta)
{
    double duration = media_duration(&app.media);

    if (!app.media.ready || duration <= 0.0)
        return;

    media_seek(&app.media,
               clampd(media_position(&app.media) + delta, 0.0, MAX(duration - 0.5, 0.0)));
    ui_request_redraw();
}

void player_cycle_repeat(void)
{
    app.repeat = (app.repeat + 1) % 3;
    g_message("repeat: %s", config_repeat_name(app.repeat));
    ui_request_redraw();
}

void player_toggle_shuffle(void)
{
    app.shuffle = !app.shuffle;

    if (app.shuffle && app.track >= 0) {
        app.history_len = 0;
        history_push(app.track);
    }

    g_message("shuffle: %s", app.shuffle ? "on" : "off");
    ui_request_redraw();
}

/* 缓存直链失效：解码器报错后失效解析缓存并重新解析一次。
 * 只重试一次，避免死链反复拉 API。 */
void player_check_load_failure(void)
{
    const AudioTrack *track;

    if (app.track < 0 || !app.source_cached || app.source_retried ||
        app.source_pending || media_is_loading(&app.media))
        return;

    if (media_error(&app.media) == NULL)
        return;

    track = config_track_at(config_get(), (gsize)app.track);
    if (track == NULL || !track->link_remote)
        return;

    g_message("play: cached link failed (%s), re-resolving once",
              media_error(&app.media));

    app.source_cached  = FALSE;
    app.source_retried = TRUE;
    app.source_pending = TRUE;

    source_cache_invalidate(track->link);
    resolve_start(track->link, app.resolve_gen);
}

void player_check_finished(void)
{
    if (!media_is_finished(&app.media))
        return;

    /* 先把这个事件消费掉：下一首要走解析/下载，期间 state 还是上一首的 */
    media_ack_finished(&app.media);

    if (app.repeat == REPEAT_ONE) {
        if (app.media.has_stream) {
            media_seek(&app.media, 0.0);
            media_continue(&app.media);
        } else {
            player_play_index(app.track);
        }
        return;
    }

    if (app.shuffle && play_random())
        return;

    if (app.repeat == REPEAT_ALL) {
        player_play_neighbour(+1);
        return;
    }

    /* 顺序播放且不循环：最后一首就停下 */
    if (app.track + 1 < (gint)config_get()->n_tracks) {
        player_play_neighbour(+1);
    } else {
        g_message("playlist: reached the end");
        media_stop(&app.media);
        ui_request_redraw();
    }
}

void player_clear(void)
{
    app.resolve_gen++;              /* 作废还在解析的请求 */
    g_clear_pointer(&app.resolved_title, g_free);
    g_clear_pointer(&app.resolved_artist, g_free);
    g_clear_pointer(&app.source_error, g_free);
}
