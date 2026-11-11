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

#include "lyricview.h"

#include <string.h>

#include <gtk/gtk.h>

#include "cache.h"
#include "lyrics.h"
#include "lyricsrc.h"
#include "ui.h"

/* 换行过渡时长（秒）：上一行上浮淡出，新行从下面浮上来 */
#define LYRIC_FADE_SEC 0.42

/* 在音频所在目录里找一个 .lrc */
static gchar *find_lrc_in_dir(const char *dir, const char *stem)
{
    g_autoptr(GDir) d           = NULL;
    gchar          *prefix_hit  = NULL;
    gchar          *only_hit    = NULL;
    const gchar    *name;
    gint            count       = 0;

    if (dir == NULL || stem == NULL)
        return NULL;

    d = g_dir_open(dir, 0, NULL);
    if (d == NULL)
        return NULL;

    while ((name = g_dir_read_name(d)) != NULL) {
        g_autofree gchar *full = NULL;

        if (!g_str_has_suffix(name, ".lrc"))
            continue;

        count++;
        full = g_build_filename(dir, name, NULL);

        if (g_str_has_prefix(name, stem)) {
            g_free(prefix_hit);
            prefix_hit = g_steal_pointer(&full);
        } else if (only_hit == NULL) {
            only_hit = g_strdup(full);
        }
    }

    if (prefix_hit != NULL)
        return prefix_hit;

    if (count == 1)
        return only_hit;

    g_free(only_hit);
    return NULL;
}

/* 在线歌词源（后台线程 + 缓存） */

typedef struct {
    guint    gen;
    gchar   *provider;
    gchar   *spec;
    gchar   *artist;
    gchar   *title;
    gchar   *text;        /* 取回来的 LRC 文本 */
    gchar   *error;
    gboolean from_cache;
} LyricsJob;

/* 缓存路径：<user cache>/follow/lyrics-<md5>.lrc */
static gchar *lyrics_cache_path(const LyricsJob *job)
{
    g_autofree gchar *key = g_strdup_printf("%s|%s|%s|%s",
                                            job->provider,
                                            job->spec != NULL ? job->spec : "",
                                            job->artist != NULL ? job->artist : "",
                                            job->title != NULL ? job->title : "");

    return follow_cache_file("lyrics", key, "lrc");
}

static gboolean lyrics_done(gpointer data)
{
    LyricsJob *job = data;

    if (job->gen == app.lyrics_gen) {
        app.lyrics_pending = FALSE;

        if (job->text != NULL) {
            lyrics_free(app.lyrics);
            app.lyrics = lyrics_load_text(job->text);

            if (app.lyrics == NULL) {
                g_free(app.lyric_error);
                app.lyric_error = g_strdup("歌词源返回的内容没有时间轴");
            } else {
                g_free(app.lyric_error);
                app.lyric_error = NULL;
                g_message("lyrics: %" G_GSIZE_FORMAT " lines (%s)%s",
                          app.lyrics->n_lines,
                          app.lyrics_spec != NULL ? app.lyrics_spec : "?",
                          job->from_cache ? " [cache]" : "");
            }
        } else {
            g_free(app.lyric_error);
            app.lyric_error = g_strdup(job->error != NULL ? job->error : "取歌词失败");
        }

        ui_request_redraw();
    }

    g_free(job->provider);
    g_free(job->spec);
    g_free(job->artist);
    g_free(job->title);
    g_free(job->text);
    g_free(job->error);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer lyrics_thread(gpointer data)
{
    LyricsJob *job    = data;
    gchar     *cached = lyrics_cache_path(job);

    if (g_file_test(cached, G_FILE_TEST_EXISTS)) {
        if (!g_file_get_contents(cached, &job->text, NULL, NULL))
            job->text = NULL;
        job->from_cache = TRUE;
    } else {
        LyricsQuery query = { job->provider, job->spec, job->artist, job->title };

        job->text = lyricsrc_fetch(&query, &job->error);

        if (job->text != NULL)
            g_file_set_contents(cached, job->text, -1, NULL);
    }

    g_free(cached);

    g_idle_add(lyrics_done, job);
    return NULL;
}

/* 音频时长和歌词总长差得多 → 提示可能是不同版本 */
static void report_fit(void)
{
    double duration, lrc_end, diff;

    if (app.lyrics == NULL || app.lyrics->n_lines == 0)
        return;

    duration = media_duration(&app.media);
    if (duration <= 0.0)
        return;

    lrc_end = (double)app.lyrics->lines[app.lyrics->n_lines - 1].time_ms / 1000.0;
    diff    = duration - lrc_end;

    if (lrc_end > duration * 1.15) {
        g_warning("lyrics: 歌词（%.0fs）比音频（%.0fs）长很多，大概率不是同一个版本，"
                  "换个歌词源更靠谱（lrc=qq:... 或本地 .lrc）", lrc_end, duration);
    } else if (diff > 8.0) {
        g_message("lyrics: 音频比歌词长 %.0fs（片头/片尾？）—— 用 [ 或 ] 调偏移，"
                  "或用 lrc_offset=%.0f 固定下来", diff, diff * 1000.0);
    } else if (diff < -8.0) {
        g_message("lyrics: 歌词比音频长 %.0fs —— 用 ] 把歌词提前，或 lrc_offset=%.0f",
                  -diff, diff * 1000.0);
    }
}

void lyricview_load(const AudioTrack *track)
{
    g_autofree gchar *audio = NULL;
    g_autofree gchar *dir   = NULL;
    g_autofree gchar *stem  = NULL;

    g_clear_pointer(&app.lyric_line, g_free);
    g_clear_pointer(&app.lyric_next, g_free);
    g_clear_pointer(&app.lyric_prev, g_free);
    g_clear_pointer(&app.lyric_error, g_free);
    g_clear_pointer(&app.lyrics, lyrics_free);

    fx_stop(&app.fx_lyric);
    app.lyric_index = -1;

    app.lyrics_pending     = FALSE;
    app.lyrics_fit_checked = FALSE;
    app.lrc_offset_base    = track != NULL ? track->lrc_offset_ms : 0;
    app.lrc_offset_ms      = app.lrc_offset_base;

    if (track == NULL)
        return;

    app.lyrics_gen++;

    /* 1) 在线歌词源：lrc=netease:... / lrc=qq:... */
    if (track->lrc != NULL && track->lrc[0] != 0) {
        g_autofree gchar *provider = NULL;
        g_autofree gchar *spec     = NULL;

        if (lyricsrc_parse(track->lrc, &provider, &spec)) {
            LyricsJob *job = g_new0(LyricsJob, 1);

            job->gen      = app.lyrics_gen;
            job->provider = g_steal_pointer(&provider);
            job->spec     = g_steal_pointer(&spec);
            job->artist   = g_strdup(track->artist);
            job->title    = g_strdup(track->title);

            /* 没写 title 就退回文件名（只对本地曲目有意义） */
            if (job->title == NULL && track->link != NULL && !track->link_remote)
                job->title = config_track_title(track);

            g_free(app.lyrics_spec);
            app.lyrics_spec    = g_strdup(track->lrc);
            app.lyrics_pending = TRUE;

            g_message("lyrics: resolving %s", track->lrc);
            g_thread_unref(g_thread_new("follow-lyrics", lyrics_thread, job));
            return;
        }
    }

    /* 本地音频的目录和文件名（不含扩展名）：找同名 .lrc，也用来解析相对路径 */
    if (track->link != NULL && !track->link_remote) {
        audio = config_to_local_path(track->link);

        if (audio != NULL) {
            gchar *dot;

            dir  = g_path_get_dirname(audio);
            stem = g_path_get_basename(audio);

            dot = strrchr(stem, '.');
            if (dot != NULL && dot != stem)
                *dot = '\0';
        }
    }

    /* 2) ini 里的 lrc=（本地文件） */
    if (track->lrc != NULL && *track->lrc != '\0') {
        g_autofree gchar *chosen = NULL;

        if (track->lrc_remote) {
            app.lyric_error = g_strdup("远程 .lrc 还不会下载");
        } else {
            g_autofree gchar *candidate = config_to_local_path(track->lrc);

            if (candidate != NULL && g_file_test(candidate, G_FILE_TEST_EXISTS)) {
                chosen = g_steal_pointer(&candidate);
            } else if (dir != NULL && !g_path_is_absolute(track->lrc)) {
                /* 相对路径：按当前目录找不到，就按音频所在目录再试一次 */
                g_autofree gchar *alt = g_build_filename(dir, track->lrc, NULL);

                if (g_file_test(alt, G_FILE_TEST_EXISTS))
                    chosen = g_steal_pointer(&alt);
            }

            if (chosen == NULL)
                app.lyric_error = g_strdup_printf("找不到歌词：%s", track->lrc);
        }

        if (chosen != NULL) {
            app.lyrics = lyrics_load_file(chosen);

            if (app.lyrics == NULL) {
                g_free(app.lyric_error);
                app.lyric_error = g_strdup_printf("歌词解析失败：%s", chosen);
            } else {
                g_message("lyrics: %" G_GSIZE_FORMAT " lines from %s",
                          app.lyrics->n_lines, chosen);
            }
            return;
        }
    }

    /* 3) 音频旁边的同名 .lrc（ini 没写、或者写了但文件不存在） */
    if (dir != NULL && stem != NULL) {
        g_autofree gchar *found = find_lrc_in_dir(dir, stem);

        if (found != NULL) {
            app.lyrics = lyrics_load_file(found);

            if (app.lyrics == NULL)
                app.lyric_error = g_strdup_printf("歌词解析失败：%s", found);
            else
                g_message("lyrics: %" G_GSIZE_FORMAT " lines from %s",
                          app.lyrics->n_lines, found);
        }
    }
}

/* 两个歌词字符串是不是同一句（NULL = 没歌词） */
static gboolean text_changed(const gchar *a, const gchar *b)
{
    if (a == NULL || b == NULL)
        return (a == NULL) != (b == NULL);

    return strcmp(a, b) != 0;
}

void lyricview_sync(double position_sec)
{
    const gchar *cur;
    const gchar *next;
    double       at;
    gboolean     changed = FALSE;

    if (!app.lyrics_fit_checked && app.lyrics != NULL &&
        media_duration(&app.media) > 0.0) {
        app.lyrics_fit_checked = TRUE;
        report_fit();
    }

    /* 歌词时间轴是按原始音频做的；视频源可能带片头/剪辑，
     * 所以用 offset 把播放位置平移一下 */
    at = position_sec - (double)app.lrc_offset_ms / 1000.0;

    {
        /* 焦点行：跳过只有时间戳的空行（前奏 / 间奏占位），
         * 句与句之间、长间奏里上一句都留在场上，不会整块空掉 */
        gssize index = lyrics_focus_at(app.lyrics, at);

        cur  = lyrics_text_at(app.lyrics, index);
        next = lyrics_text_at(app.lyrics, lyrics_next_index(app.lyrics, index));

        if (index != app.lyric_index) {
            app.lyric_index = index;
            changed         = TRUE;
        }
    }

    if (text_changed(cur, app.lyric_line)) {
        /* 旧的当前行交给 prev：过渡时上浮淡出，新行淡入 */
        g_free(app.lyric_prev);
        app.lyric_prev = app.lyric_line;
        app.lyric_line = cur != NULL ? g_strdup(cur) : NULL;

        if (app.lyric_line == NULL) {
            fx_stop(&app.fx_lyric);
        } else if (app.lyric_prev != NULL && strcmp(app.lyric_line, app.lyric_prev) == 0) {
            /* 同一句重复（副歌）：画面本来就不用动 */
            fx_stop(&app.fx_lyric);
        } else {
            fx_begin(&app.fx_lyric, LYRIC_FADE_SEC);
            ui_animate();
        }

        changed = TRUE;
    }

    if (text_changed(next, app.lyric_next)) {
        g_free(app.lyric_next);
        app.lyric_next = next != NULL ? g_strdup(next) : NULL;
        changed = TRUE;
    }

    if (changed)
        ui_request_redraw();
}

void lyricview_shift_offset(int delta_ms)
{
    app.lrc_offset_ms += delta_ms;
    g_message("lyrics offset: %+d ms", app.lrc_offset_ms);
    ui_request_redraw();
}

void lyricview_reset_offset(void)
{
    app.lrc_offset_ms = app.lrc_offset_base;
    g_message("lyrics offset: reset to %+d ms", app.lrc_offset_ms);
    ui_request_redraw();
}

void lyricview_clear(void)
{
    g_clear_pointer(&app.lyrics, lyrics_free);
    g_clear_pointer(&app.lyric_line, g_free);
    g_clear_pointer(&app.lyric_next, g_free);
    g_clear_pointer(&app.lyric_prev, g_free);
    g_clear_pointer(&app.lyric_error, g_free);
    g_clear_pointer(&app.lyrics_spec, g_free);

    fx_stop(&app.fx_lyric);
}
