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

/* 一段带时间的词：增强 LRC 的 <mm:ss.xx>、网易云 yrc 的 (start,dur,0) */
typedef struct {
    gint64 ms;      /* 起始时间 */
    gint64 end_ms;  /* 结束时间（0 = 未知，用下一个词的起始 / 行尾） */
    guint  off;     /* 在整行文本里的字节偏移 */
    guint  len;     /* 字节数 */
} RawWord;

static GArray *words_new(void)
{
    return g_array_new(FALSE, TRUE, sizeof(RawWord));
}

/* 表项可能是 NULL（这行没有逐字时间），释放时要能接住 */
static void words_table_free(gpointer p)
{
    if (p != NULL)
        g_array_unref(p);
}

static void word_add(GArray *words, gint64 ms, gint64 end_ms, guint off, guint len)
{
    RawWord w;

    if (len == 0)
        return;

    w.ms     = ms;
    w.end_ms = end_ms;
    w.off    = off;
    w.len    = len;
    g_array_append_val(words, w);
}

/* 把每段的结束时间补成"下一个词的起始"（最后一段留 0 = 未知） */
static void words_fill_ends(GArray *words)
{
    for (guint i = 0; i + 1 < words->len; i++) {
        RawWord *cur = &g_array_index(words, RawWord, i);

        if (cur->end_ms <= cur->ms)
            cur->end_ms = g_array_index(words, RawWord, i + 1).ms;
    }
}

/* 去掉行内的 <mm:ss.xx> 标记，同时把每个 <...> 之后的文本段收成一个词：
 *   [00:12.00]<00:12.00>あ<00:12.50>い → 两个词，各有自己的起始时间 */
static gchar *strip_inline_words(const gchar *text, gint64 line_start, GArray **out_words)
{
    GString *out = g_string_sized_new(strlen(text) + 1);
    GArray  *words = words_new();
    gint64   pending = line_start;
    guint    seg_start = 0;
    const gchar *p = text;

    while (*p != '\0') {
        if (*p == '<') {
            const gchar *close = strchr(p, '>');

            if (close != NULL) {
                g_autofree gchar *inner = g_strndup(p + 1, (gsize)(close - p - 1));
                gint64             ms = 0;

                if (parse_timestamp(inner, &ms)) {
                    /* 收尾上一段（行首的 <t> 之前那段没有自己的时间，用行起始） */
                    word_add(words, pending, 0, seg_start, out->len - seg_start);

                    pending   = ms;
                    seg_start = out->len;
                    p         = close + 1;
                    continue;
                }
            }
        }

        g_string_append_c(out, *p++);
    }

    word_add(words, pending, 0, seg_start, out->len - seg_start);
    words_fill_ends(words);

    if (words->len < 2) {
        g_array_unref(words);
        words = NULL;
    }

    if (out_words != NULL)
        *out_words = words;
    else if (words != NULL)
        g_array_unref(words);

    return g_string_free(out, FALSE);
}

/* 解析一个没有前导的整数（yrc 用），成功返回 TRUE 并前移 *pp */
static gboolean parse_bare_int(const gchar **pp, gint64 *out)
{
    const gchar *p = *pp;
    gint64       v = 0;
    int          digits = 0;

    while (g_ascii_isdigit(*p)) {
        v = v * 10 + (*p - '0');
        p++;
        digits++;
    }

    if (digits == 0)
        return FALSE;

    *out = v;
    *pp  = p;
    return TRUE;
}

/* yrc 的一个词头：(start,dur[,flags]) */
static gboolean parse_word_group(const gchar **pp, gint64 *start, gint64 *end_ms)
{
    const gchar *p = *pp;
    gint64       dur = 0, flags = 0;

    if (*p != '(')
        return FALSE;
    p++;

    if (!parse_bare_int(&p, start) || *p != ',')
        return FALSE;
    p++;
    if (!parse_bare_int(&p, &dur))
        return FALSE;

    if (*p == ',') {
        p++;
        if (!parse_bare_int(&p, &flags))
            return FALSE;
    }

    if (*p != ')')
        return FALSE;
    p++;

    *end_ms = dur > 0 ? *start + dur : 0;
    *pp     = p;
    return TRUE;
}

/* 网易云逐字歌词（yrc）：
 *   [行起始,行时长](词起始,词时长,0)字(词起始,词时长,0)字...
 * 词时间是绝对毫秒。返回 TRUE 表示这行是 yrc。 */
static gboolean parse_yrc_line(const gchar *line, gint64 *out_start,
                               gchar **out_text, GArray **out_words)
{
    const gchar *p = line;
    gint64       start = 0, dur = 0;
    GString     *text;
    GArray      *words;

    if (*p != '[')
        return FALSE;

    p++;
    if (!parse_bare_int(&p, &start) || *p != ',')
        return FALSE;
    p++;
    if (!parse_bare_int(&p, &dur) || *p != ']')
        return FALSE;
    p++;

    text  = g_string_new(NULL);
    words = words_new();

    while (*p != '\0') {
        const gchar *next = p;
        gint64       ws = 0, we = 0;
        guint        off;

        if (!parse_word_group(&next, &ws, &we)) {
            /* 后面不是词头了：把剩下的文本补到最后一个词上，别把歌词吃掉 */
            if (words->len > 0) {
                RawWord *last = &g_array_index(words, RawWord, words->len - 1);

                g_string_append(text, p);
                last->len = text->len - last->off;
            }
            break;
        }

        off = text->len;
        p   = next;      /* 文本从词头后面开始 */

        /* 这一段一直延伸到下一个词头（'(数字'）为止 */
        {
            const gchar *seg_end = p;

            while (*seg_end != '\0') {
                if (*seg_end == '(' && g_ascii_isdigit(seg_end[1]))
                    break;
                seg_end++;
            }

            g_string_append_len(text, p, (gssize)(seg_end - p));
            word_add(words, ws, we, off, (guint)(seg_end - p));
            p = seg_end;
        }
    }

    if (words->len == 0) {
        g_array_unref(words);
        g_string_free(text, TRUE);
        return FALSE;
    }

    words_fill_ends(words);

    if (words->len < 2) {
        g_array_unref(words);
        words = NULL;
    }

    *out_start = start;
    *out_text  = g_string_free(text, FALSE);
    *out_words = words;
    (void)dur;
    return TRUE;
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
    GPtrArray *words;      /* GArray<RawWord>* 或 NULL，和 texts 一一对应 */
    gchar     *title;
    gchar     *artist;
} RawLyrics;

static void raw_free(RawLyrics *raw)
{
    if (raw == NULL)
        return;

    g_ptr_array_unref(raw->texts);
    g_array_unref(raw->times);
    g_ptr_array_unref(raw->words);
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
    raw->words = g_ptr_array_new_with_free_func(words_table_free);

    raw_lines = g_strsplit(content, "\n", -1);

    for (gchar **line = raw_lines; *line != NULL; line++) {
        gchar  *cursor = g_strstrip(*line);
        GArray *stamps = g_array_new(FALSE, TRUE, sizeof(gint64));
        gchar  *text;
        gint64  offset_ms = 0;

        /* 网易云逐字歌词（yrc）：整行自带词级时间轴，不用再走 LRC 那套 */
        {
            g_autofree gchar *ytext = NULL;
            GArray           *ywords = NULL;
            gint64            ystart = 0;

            if (parse_yrc_line(cursor, &ystart, &ytext, &ywords)) {
                g_ptr_array_add(raw->texts, g_steal_pointer(&ytext));
                g_array_append_val(raw->times, ystart);
                g_ptr_array_add(raw->words, ywords);

                g_array_free(stamps, TRUE);
                continue;
            }
        }

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

        {
            GArray *words = NULL;
            gint64  line_start = stamps->len > 0
                ? g_array_index(stamps, gint64, 0) - offset_ms : 0;

            text = strip_inline_words(cursor, line_start, &words);

            if (*text == '\0' && stamps->len == 0) {
                g_free(text);
                if (words != NULL)
                    g_array_unref(words);
                g_array_free(stamps, TRUE);
                continue;      /* 空行 / 纯元数据行 */
            }

            if (stamps->len == 0) {
                /* 没有时间戳：也算一行，等打轴 */
                gint64 missing = -1;

                g_ptr_array_add(raw->texts, text);
                g_array_append_val(raw->times, missing);
                g_ptr_array_add(raw->words, words);
            } else {
                for (guint i = 0; i < stamps->len; i++) {
                    gint64 stamp = g_array_index(stamps, gint64, i) - offset_ms;

                    g_ptr_array_add(raw->texts, g_strdup(text));
                    g_array_append_val(raw->times, stamp);
                    g_ptr_array_add(raw->words, words != NULL ? g_array_copy(words) : NULL);
                }

                g_free(text);
                if (words != NULL)
                    g_array_unref(words);
            }
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

/* 构建 + 排序用：一行歌词 + 它自己的词时间表 */
typedef struct {
    gint64  time_ms;
    gchar  *text;
    GArray *words;      /* 借用 raw->words 里的元素 */
} TmpLine;

static gint compare_lines(const void *a, const void *b, gpointer user_data)
{
    const TmpLine *la = (const TmpLine *)a;
    const TmpLine *lb = (const TmpLine *)b;

    (void)user_data;

    if (la->time_ms < lb->time_ms) return -1;
    if (la->time_ms > lb->time_ms) return 1;
    return 0;
}

/* 把一行的词时间表摊平成"每个字的绝对时间"。
 * 词内多个字按词时长线性均分；没有逐字时间的行返回 NULL（渲染时整行一起亮）。 */
static gint64 *build_char_times(const gchar *text, GArray *words,
                               gint64 line_start, gint64 line_end, gsize *out_n)
{
    gsize   n = g_utf8_strlen(text, -1);
    gint64 *times;
    gsize   last = 0;
    gint64  last_end;

    *out_n = n;

    if (n == 0 || words == NULL || words->len < 2)
        return NULL;

    times = g_new(gint64, n);

    /* 先按"整行起始"铺底，词没覆盖到的字就不会是垃圾值 */
    for (gsize i = 0; i < n; i++)
        times[i] = line_start;

    for (guint w = 0; w < words->len; w++) {
        const RawWord *word = &g_array_index(words, RawWord, w);
        gint64         ws = word->ms;
        gint64         we = word->end_ms > word->ms ? word->end_ms : 0;
        gsize          first = g_utf8_strlen(text, (gssize)word->off);
        gsize          count = g_utf8_strlen(text + word->off, (gssize)word->len);

        if (count == 0)
            continue;

        if (we <= ws)
            we = w + 1 < words->len ? g_array_index(words, RawWord, w + 1).ms : line_end;
        if (we < ws)
            we = ws;

        for (gsize i = 0; i < count && first + i < n; i++) {
            times[first + i] = count > 1
                ? ws + (gint64)((double)(we - ws) * (double)i / (double)(count - 1))
                : ws;
        }

        if (first + count > last) {
            last     = first + count;
            last_end = we;
        }
    }

    /* 整行都没算出来（词时间无效）：当作"整行一起亮" */
    if (last == 0) {
        g_free(times);
        return NULL;
    }

    /* 词没盖住的尾巴：接在最后一个词的结束时间上 */
    for (gsize i = last; i < n; i++)
        times[i] = last_end;

    return times;
}

static Lyrics *lyrics_from_raw(RawLyrics *raw)
{
    Lyrics  *lyrics;
    TmpLine *tmp;
    gsize    n = 0;

    if (raw == NULL)
        return NULL;

    tmp = g_new0(TmpLine, raw->texts->len + 1);

    /* 只保留有时间戳的行（纯文本行没有时间，播放时没法用） */
    for (guint i = 0; i < raw->texts->len; i++) {
        gint64 ms = g_array_index(raw->times, gint64, i);

        if (ms < 0)
            continue;

        tmp[n].time_ms = ms;
        tmp[n].text    = g_ptr_array_index(raw->texts, i);
        tmp[n].words   = g_ptr_array_index(raw->words, i);
        n++;
    }

    g_sort_array(tmp, n, sizeof(TmpLine), compare_lines, NULL);

    if (n == 0) {
        g_free(tmp);
        raw_free(raw);
        return NULL;
    }

    lyrics         = g_new0(Lyrics, 1);
    lyrics->title  = g_strdup(raw->title);
    lyrics->artist = g_strdup(raw->artist);
    lyrics->lines  = g_new0(LyricLine, n);

    for (gsize i = 0; i < n; i++) {
        gint64 line_end = i + 1 < n ? tmp[i + 1].time_ms : tmp[i].time_ms + 4000;

        lyrics->lines[i].time_ms = tmp[i].time_ms;
        lyrics->lines[i].text    = g_strdup(tmp[i].text);
        lyrics->lines[i].char_ms = build_char_times(tmp[i].text, tmp[i].words,
                                                    tmp[i].time_ms, line_end,
                                                    &lyrics->lines[i].n_chars);
        lyrics->n_lines++;
    }

    /* tmp 只借用 raw 的字符串，不负责释放 */
    g_free(tmp);
    raw_free(raw);

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

    for (gsize i = 0; i < lyrics->n_lines; i++) {
        g_free(lyrics->lines[i].text);
        g_free(lyrics->lines[i].char_ms);
    }

    g_free(lyrics->lines);
    g_free(lyrics->title);
    g_free(lyrics->artist);
    g_free(lyrics);
}

const gchar *lyrics_text_at(const Lyrics *lyrics, gssize index)
{
    if (lyrics == NULL || index < 0 || index >= (gssize)lyrics->n_lines)
        return NULL;

    return lyrics->lines[index].text;
}

const gint64 *lyrics_char_times(const Lyrics *lyrics, gssize index, gsize *n_chars)
{
    if (n_chars != NULL)
        *n_chars = 0;

    if (lyrics == NULL || index < 0 || index >= (gssize)lyrics->n_lines)
        return NULL;

    if (lyrics->lines[index].char_ms == NULL)
        return NULL;

    if (n_chars != NULL)
        *n_chars = lyrics->lines[index].n_chars;

    return lyrics->lines[index].char_ms;
}

gssize lyrics_index_at(const Lyrics *lyrics, double seconds)
{
    gint64 at_ms;
    gsize  lo, hi;

    if (lyrics == NULL || lyrics->n_lines == 0)
        return -1;

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

    return (gssize)lo - 1;
}

/* 空行也算一行（很多 LRC 用只有时间戳的空行标前奏 / 间奏），
 * 但要当"有没有字"来判断 */
static gboolean line_has_text(const Lyrics *lyrics, gsize index)
{
    const gchar *text = lyrics->lines[index].text;

    if (text == NULL)
        return FALSE;

    for (const gchar *p = text; *p != '\0'; p++) {
        if (!g_ascii_isspace(*p))
            return TRUE;
    }

    return FALSE;
}

const gchar *lyrics_line_at(const Lyrics *lyrics, double seconds)
{
    gssize index = lyrics_index_at(lyrics, seconds);

    return index < 0 ? NULL : lyrics->lines[index].text;
}

gssize lyrics_next_index(const Lyrics *lyrics, gssize index)
{
    if (lyrics == NULL)
        return -1;

    for (gsize i = (gsize)(index + 1); i < lyrics->n_lines; i++) {
        if (line_has_text(lyrics, i))
            return (gssize)i;
    }

    return -1;
}

gssize lyrics_prev_index(const Lyrics *lyrics, gssize index)
{
    if (lyrics == NULL)
        return -1;

    for (gssize i = MIN(index, (gssize)lyrics->n_lines) - 1; i >= 0; i--) {
        if (line_has_text(lyrics, (gsize)i))
            return i;
    }

    return -1;
}

gssize lyrics_focus_at(const Lyrics *lyrics, double seconds)
{
    gssize index = lyrics_index_at(lyrics, seconds);

    if (index < 0)
        return -1;

    /* 当前正好落在“只有时间戳”的空行上（前奏 / 间奏 / 换段占位）：
     * 上一句保持在场，不把整块歌词擦掉（AMLL 的 highlightedGroups 也是这个语义） */
    if (line_has_text(lyrics, (gsize)index))
        return index;

    return lyrics_prev_index(lyrics, index + 1);
}

const gchar *lyrics_line_next(const Lyrics *lyrics, double seconds)
{
    gssize index;

    if (lyrics == NULL || lyrics->n_lines == 0)
        return NULL;

    index = lyrics_focus_at(lyrics, seconds);
    index = lyrics_next_index(lyrics, index);

    return index < 0 ? NULL : lyrics->lines[index].text;
}
