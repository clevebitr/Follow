#ifndef FOLLOW_CONFIG_H
#define FOLLOW_CONFIG_H

#include <glib.h>

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE  "Follow"
#define QRCODE_LINK   "https://github.com/clevebitr"

/* 一个曲目 = 配置里的一个 [audioN] 组。
 * link / pic / lrc 既可以是本地路径，也可以是 http(s) URL；
 * *_remote 是加载时算好的标志，省得每次再解析 scheme。
 * 所有字段的内存都由 config 模块持有，调用方不要 free。 */
typedef struct {
    gchar   *id;          /* 组名，如 "audio1" */
    gchar   *link;        /* 音频：本地路径 或 http(s) URL，必有 */
    gchar   *pic;         /* 封面：可为 NULL */
    gchar   *lrc;         /* 歌词：可为 NULL */
    gboolean link_remote;
    gboolean pic_remote;
    gboolean lrc_remote;
} AudioTrack;

typedef struct {
    gint     width;
    gint     height;
    gboolean fullscreen;
    gchar   *title;
    gchar   *qrcode_link;

    /* [audio1], [audio2] ... 按数字顺序排好的曲目表 */
    AudioTrack *tracks;
    gsize       n_tracks;
} APPconfig;

/* 读取配置文件（不存在 / 缺 key / 读失败都用默认值补齐）。
 * 返回进程内单例，恒非 NULL，调用方不要 free。 */
const APPconfig *config_load(const char *path);

/* 取配置；没 load 过就先返回一份默认值。 */
const APPconfig *config_get(void);

/* 释放内部字符串和曲目表，程序退出时调一次即可（可重复调用）。 */
void config_free(void);

/* 写一份默认配置（含音频段示例），用于首次运行时生成模板。 */
void config_save_default(const char *path);

/* 取第 index 个曲目；越界返回 NULL。 */
const AudioTrack *config_track_at(const APPconfig *cfg, gsize index);

/* uri 是不是 http/https 远程地址（没有 scheme 的按本地路径处理）。 */
gboolean config_is_remote(const gchar *uri);

/* 把本地路径 / file:// URI 统一成文件系统路径，返回新分配的字符串。 */
gchar *config_to_local_path(const gchar *uri);

#endif /* FOLLOW_CONFIG_H */
