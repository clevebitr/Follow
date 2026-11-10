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
    gchar   *id;            /* 组名，如 "audio1" */
    gchar   *link;          /* 音频：本地路径 或 http(s) URL，必有 */
    gchar   *title;         /* 曲名：可为 NULL（从文件名推断） */
    gchar   *artist;        /* 艺人：可为 NULL */
    gchar   *pic;           /* 封面：可为 NULL */
    gchar   *lrc;           /* 歌词：本地 .lrc / netease:... / qq:...，可为 NULL */
    gint     lrc_offset_ms; /* 歌词整体偏移（毫秒）：正数 = 歌词延后 */
    gchar   *page;          /* 平台页面链接（二维码 + 徽标用）：可为 NULL */
    gchar   *note;          /* 这首歌的备注（覆盖 [note] text），可为 NULL */
    gchar   *platform;      /* 平台短名，可为 NULL（缺省按 page 的域名自动识别） */
    gboolean link_remote;
    gboolean pic_remote;
    gboolean lrc_remote;
} AudioTrack;

/* 循环模式 */
typedef enum {
    REPEAT_OFF = 0,     /* 放到列表末尾就停 */
    REPEAT_ALL,         /* 列表循环 */
    REPEAT_ONE          /* 单曲循环 */
} RepeatMode;

typedef struct {
    gint     width;
    gint     height;
    gboolean fullscreen;
    gchar   *title;
    gchar   *qrcode_link;
    gchar   *qrcode_label;   /* 自定义链接胶囊上的文字，可为 NULL */
    gchar   *note;           /* [note] text= 全局备注：可为 NULL */

    /* [playback] 播放偏好 */
    gint     repeat;        /* RepeatMode */
    gboolean shuffle;

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

/* 备注文字：曲目自己的 note 优先，否则用全局 [note] text；都没有返回 NULL */
const gchar *config_track_note(const APPconfig *cfg, const AudioTrack *track);

/* 循环模式的短名："off" / "all" / "one" */
const gchar *config_repeat_name(gint repeat);

/* 取第 index 个曲目；越界返回 NULL。 */
const AudioTrack *config_track_at(const APPconfig *cfg, gsize index);

/* 显示用曲名：title 键优先，其次从文件名/main.exe 里推断；返回新分配的字符串。 */
gchar *config_track_title(const AudioTrack *track);

/* 显示用「艺人 - 曲名」；没有 artist 时只返回曲名。返回新分配的字符串。 */
gchar *config_track_label(const AudioTrack *track);

/* uri 是不是 http/https 远程地址（没有 scheme 的按本地路径处理）。 */
gboolean config_is_remote(const gchar *uri);

/* 把本地路径 / file:// URI 统一成文件系统路径，返回新分配的字符串。 */
gchar *config_to_local_path(const gchar *uri);

#endif /* FOLLOW_CONFIG_H */
