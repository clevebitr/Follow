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

#include <string.h>

#include "lyrics.h"

/* [mm:ss.xx] → 毫秒。解析成功返回 TRUE。 */
static gboolean parse_timestamp(const gchar *tag, gint64 *out_ms)
{
    const gchar *p = tag;
    gint64       minutes = 0, seconds = 0, frac = 0;
    gint         digits;

    while (g_ascii_isdigit(*p)) {
        minutes = minutes * 10 + (*p - '0');
        p++;
    }

    if (*p != ':')
        return FALSE;
    p++;

    digits = 0;
    while (g_ascii_isdigit(*p)) {
        seconds = seconds * 10 + (*p - '0');
        p++;
        digits++;
    }
    if (digits == 0 || digits > 2)
        return FALSE;

    /* 小数部分：'.' 或 ':' 分隔，1..3 位（.5 = 500ms，.05 = 50ms） */
    if (*p == '.' || *p == ':') {
        gint scale = 100;

        p++;
        digits = 0;
        while (g_ascii_isdigit(*p)) {
            if (digits < 3) {
                frac += (*p - '0') * scale;
                scale /= 10;
            }
            p++;
            digits++;
        }
        if (digits == 0)
            return FALSE;
    }

    if (*p != '\0')
        return FALSE;

    *out_ms = minutes * 60000 + seconds * 1000 + frac;
    return TRUE;
}

/* 去掉行内的 <mm:ss.xx> 标记 */
static void strip_inline_timestamps(gchar *text)
{
    gchar *r = text, *w = text;

    while (*r != '\0') {
        if (*r == '<') {
            gchar *close = strchr(r, '>');

            if (close != NULL) {
                g_autofree gchar *inner = g_strndup(r + 1, (gsize)(close - r - 1));
                gint64             ms = 0;

                if (parse_timestamp(inner, &ms)) {
                    r = close + 1;
                    continue;
                }
            }
        }

        *w++ = *r++;
    }

    *w = '\0';
}

/* 读文件并统一成 UTF-8（GBK/GB18030 会自动转） */
static gchar *read_text_file(const char *path)
{
    g_autofree gchar *content = NULL;
    g_autoptr(GError) error = NULL;

    if (path == NULL)
        return NULL;

    if (!g_file_get_contents(path, &content, NULL, &error)) {
        g_warning("lyrics: cannot read %s: %s", path, error->message);
        return NULL;
    }

    if (!g_utf8_validate(content, -1, NULL)) {
        g_autoptr(GError) conv_error = NULL;
        gsize             written = 0;
        gchar            *converted = g_convert(content, -1, "UTF-8", "GB18030",
                                                NULL, &written, &conv_error);

        if (converted != NULL) {
            g_free(content);
            content = converted;
            g_message("lyrics: %s 不是 UTF-8，已按 GB18030 转换", path);
        } else {
            g_warning("lyrics: %s 编码无法识别: %s", path, conv_error->message);
        }
    }

    /* 去 BOM */
    if (g_str_has_prefix(content, "\xef\xbb\xbf"))
        memmove(content, content + 3, strlen(content + 3) + 1);

    return g_steal_pointer(&content);
}

/* 解析的中间态：按文件顺序的行 + 可能缺失的时间戳（-1）。
 * 只是本文件内部用，外面只看到排好序、只有带时间戳行的 Lyrics。 */
typedef struct {
    GPtrArray *texts;      /* gchar* */
    GArray    *times;      /* gint64，-1 = 这行没有时间戳 */
    gchar     *title;
    gchar     *artist;
} RawLyrics;

static void raw_free(RawLyrics *raw)
{
    if (raw == NULL)
        return;

    g_ptr_array_unref(raw->texts);
    g_array_unref(raw->times);
    g_free(raw->title);
    g_free(raw->artist);
    g_free(raw);
}

/* 从一段文本解析出原始行（time = -1 表示这行没有时间戳） */
static RawLyrics *raw_from_text(const gchar *content)
{
    RawLyrics        *raw;
    gchar           **raw_lines;

    if (content == NULL)
        return NULL;

    raw        = g_new0(RawLyrics, 1);
    raw->texts = g_ptr_array_new_with_free_func(g_free);
    raw->times = g_array_new(FALSE, TRUE, sizeof(gint64));

    raw_lines = g_strsplit(content, "\n", -1);

    for (gchar **line = raw_lines; *line != NULL; line++) {
        gchar  *cursor = g_strstrip(*line);
        GArray *stamps = g_array_new(FALSE, TRUE, sizeof(gint64));
        gchar  *text;
        gint64  offset_ms = 0;

        while (*cursor == '[') {
            gchar *close = strchr(cursor, ']');

            if (close == NULL)
                break;

            {
                g_autofree gchar *tag = g_strndup(cursor + 1, (gsize)(close - cursor - 1));
                gint64             ms = 0;

                if (g_str_has_prefix(tag, "ti:")) {
                    g_free(raw->title);
                    raw->title = g_strdup(tag + 3);
                } else if (g_str_has_prefix(tag, "ar:")) {
                    g_free(raw->artist);
                    raw->artist = g_strdup(tag + 3);
                } else if (g_str_has_prefix(tag, "offset:")) {
                    offset_ms = g_ascii_strtoll(tag + 7, NULL, 10);
                } else if (parse_timestamp(tag, &ms)) {
                    g_array_append_val(stamps, ms);
                }
            }

            cursor = close + 1;
        }

        text = g_strdup(cursor);
        strip_inline_timestamps(text);
        g_strstrip(text);

        if (*text == '\0' && stamps->len == 0) {
            g_free(text);
            g_array_free(stamps, TRUE);
            continue;      /* 空行 / 纯元数据行 */
        }

        if (stamps->len == 0) {
            /* 没有时间戳：也算一行，等打轴 */
            gint64 missing = -1;

            g_ptr_array_add(raw->texts, text);
            g_array_append_val(raw->times, missing);
        } else {
            for (guint i = 0; i < stamps->len; i++) {
                gint64 stamp = g_array_index(stamps, gint64, i) - offset_ms;

                g_ptr_array_add(raw->texts, g_strdup(text));
                g_array_append_val(raw->times, stamp);
            }

            g_free(text);
        }

        g_array_free(stamps, TRUE);
    }

    g_strfreev(raw_lines);

    if (raw->texts->len == 0) {
        raw_free(raw);
        return NULL;
    }

    return raw;
}

static gint compare_lines(const void *a, const void *b, gpointer user_data)
{
    const LyricLine *la = (const LyricLine *)a;
    const LyricLine *lb = (const LyricLine *)b;

    (void)user_data;

    if (la->time_ms < lb->time_ms) return -1;
    if (la->time_ms > lb->time_ms) return 1;
    return 0;
}

static Lyrics *lyrics_from_raw(RawLyrics *raw)
{
    Lyrics *lyrics;

    if (raw == NULL)
        return NULL;

    lyrics = g_new0(Lyrics, 1);
    lyrics->title  = g_strdup(raw->title);
    lyrics->artist = g_strdup(raw->artist);
    lyrics->lines  = g_new0(LyricLine, raw->texts->len + 1);

    /* 只保留有时间戳的行（纯文本行没有时间，播放时没法用） */
    for (guint i = 0; i < raw->texts->len; i++) {
        gint64 ms = g_array_index(raw->times, gint64, i);

        if (ms < 0)
            continue;

        lyrics->lines[lyrics->n_lines].time_ms = ms;
        lyrics->lines[lyrics->n_lines].text    = g_strdup(g_ptr_array_index(raw->texts, i));
        lyrics->n_lines++;
    }

    raw_free(raw);

    if (lyrics->n_lines == 0) {
        lyrics_free(lyrics);
        return NULL;
    }

    g_sort_array(lyrics->lines, lyrics->n_lines, sizeof(LyricLine), compare_lines, NULL);

    return lyrics;
}

Lyrics *lyrics_load_file(const char *path)
{
    g_autofree gchar *content = read_text_file(path);

    return content != NULL ? lyrics_from_raw(raw_from_text(content)) : NULL;
}

Lyrics *lyrics_load_text(const gchar *content)
{
    if (content == NULL)
        return NULL;

    /* 在线源拿到的通常是 UTF-8；万一是 GBK 也兜一下 */
    if (!g_utf8_validate(content, -1, NULL)) {
        gsize  written = 0;
        gchar *converted = g_convert(content, -1, "UTF-8", "GB18030", NULL, &written, NULL);

        if (converted == NULL)
            return NULL;

        {
            Lyrics *lyrics = lyrics_from_raw(raw_from_text(converted));

            g_free(converted);
            return lyrics;
        }
    }

    return lyrics_from_raw(raw_from_text(content));
}

void lyrics_free(Lyrics *lyrics)
{
    if (lyrics == NULL)
        return;

    for (gsize i = 0; i < lyrics->n_lines; i++)
        g_free(lyrics->lines[i].text);

    g_free(lyrics->lines);
    g_free(lyrics->title);
    g_free(lyrics->artist);
    g_free(lyrics);
}

const gchar *lyrics_line_at(const Lyrics *lyrics, double seconds)
{
    gint64 at_ms;
    gsize  lo, hi;

    if (lyrics == NULL || lyrics->n_lines == 0)
        return NULL;

    at_ms = (gint64)(seconds * 1000.0);

    lo = 0;
    hi = lyrics->n_lines;

    while (lo < hi) {
        gsize mid = (lo + hi) / 2;

        if (lyrics->lines[mid].time_ms <= at_ms)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return NULL;

    return lyrics->lines[lo - 1].text;
}
