#ifndef FOLLOW_SOURCE_H
#define FOLLOW_SOURCE_H

#include <glib.h>

/* 把 ini 里写的 link 变成"解码器能直接吃的东西"：
 *   本地路径        → 原样
 *   B 站视频页面     → 走 API 解析出音频直链 + Referer 请求头 + 标题/UP 主
 *   其它 http(s) 直链 → 原样（mp3 / flac / m3u8 等交给解码器自己处理）
 */
typedef struct {
    gchar   *url;
    gchar   *headers;   /* "Key: Value\r\n..."，可为 NULL */
    gchar   *title;     /* 网络来源解析出来的标题，可为 NULL */
    gchar   *artist;
    gboolean resolved;  /* TRUE = 走了解析器 */
} MediaSource;

/* 成功返回 0；失败返回 -1 并给出 *error（新分配的说明） */
int  source_resolve(const char *uri, MediaSource *out, gchar **error);
void source_clear(MediaSource *s);

#endif /* FOLLOW_SOURCE_H */
