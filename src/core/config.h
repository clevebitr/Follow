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

#include "keymap.h"

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE  "Follow"
#define QRCODE_LINK   "https://github.com/clevebitr"

/* [net] 没配时的网络默认值。
 * UA 用浏览器串：B 站这类接口会挡非浏览器 UA。 */
#define HTTP_DEFAULT_USER_AGENT \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
#define HTTP_DEFAULT_TIMEOUT         15   /* 整次请求，秒 */
#define HTTP_DEFAULT_CONNECT_TIMEOUT 5    /* 建连，秒 */

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

    /* [cache] 歌词 / 封面 / B 站元数据 / 收听统计的落地目录；
     * NULL = <用户缓存目录>/follow */
    gchar   *cache_dir;

    /* [cache] 解析结果 / 可选音频 / 封面缓存策略 */
    gint     resolve_ttl;      /* 秒；0 = 不缓存解析结果 */
    gboolean audio_cache;      /* 是否把完整音频落到 <cache>/follow/audio */
    gint     audio_max_mb;     /* 音频缓存上限（MB） */
    gint     cover_max_mb;     /* 封面缓存上限（MB） */

    /* [visualizer] 背景粒子效果：effect = off / dust / warp / flow */
    gchar   *viz_effect;
    gint     viz_particles;   /* 粒子数量，0 = 用默认值 */
    double   viz_gain;        /* 音频灵敏度，1.0 为基准 */

    /* [ui] 动效总开关 */
    gboolean animations;

    /* [keys] 按键绑定：每个动作的原始字符串，NULL = 用 keymap 内置默认值 */
    gchar   *key_bind[KEY_ACTION_COUNT];

    /* [net] 网络请求默认值：UA 为 NULL、超时 <= 0 时用 config.h 里的内置值 */
    gchar   *user_agent;
    gchar   *proxy;             /* 形如 http://127.0.0.1:7890；NULL = 直连 */
    gint     http_timeout;      /* 整次请求超时，秒 */
    gint     connect_timeout;   /* 建连超时，秒 */

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

/* 有效的网络参数：配置里没写就回落到内置默认（恒非 NULL / 恒 > 0） */
const gchar *config_user_agent(void);
gint         config_http_timeout(void);
gint         config_http_connect_timeout(void);

/* [net] proxy：没配置返回 NULL（直连）；空串也当没配 */
const gchar *config_proxy(void);

/* [visualizer] 的生效值：effect 恒非 NULL，particles / gain 恒为合理值 */
const gchar *config_viz_effect(void);
gint         config_viz_particles(void);
double       config_viz_gain(void);

/* [keys] 原始绑定字符串；没写返回 NULL（keymap 用默认值） */
const gchar *config_key_binding(KeyAction action);

/* [cache] 解析结果 TTL（秒，0 = 关闭）；音频 / 封面缓存上限（字节）。
 * audio 相关访问器是预留接口，当前还没有音频落盘实现。 */
gint    config_resolve_ttl(void);
gboolean config_audio_cache(void);      /* 预留 */
guint64 config_audio_max_bytes(void);   /* 预留 */
guint64 config_cover_max_bytes(void);

/* [ui] 动效总开关 */
gboolean config_animations(void);

/* 把本地路径 / file:// URI 统一成文件系统路径，返回新分配的字符串。 */
gchar *config_to_local_path(const gchar *uri);

#endif /* FOLLOW_CONFIG_H */
