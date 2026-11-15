/**
 * created: 2026/11/10
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#ifndef FOLLOW_CONFIG_H
#define FOLLOW_CONFIG_H

#include <glib.h>

#include "keymap.h"

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE  "Follow"
#define QRCODE_LINK   "https://github.com/clevebitr"

/* 默认浏览器 UA；B 站等接口会拒绝非浏览器 UA。 */
#define HTTP_DEFAULT_USER_AGENT \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
#define HTTP_DEFAULT_TIMEOUT         15   /* 请求总超时（秒） */
#define HTTP_DEFAULT_CONNECT_TIMEOUT 5    /* 连接超时（秒） */

/* MusicBrainz 要求 UA 包含项目信息，否则可能被限速。 */
#define METADATA_DEFAULT_USER_AGENT \
    "Follow/0.1.0 ( https://github.com/clevebitr/follow )"

/* 曲目：对应一个 [audioN] 组。link / pic / lrc 支持本地路径或 http(s) URL。
 * 字段由 config 模块持有，调用方不得释放。 */

typedef struct {
    gchar   *id;
    gchar   *link;
    gchar   *title;
    gchar   *artist;
    gchar   *pic;
    gchar   *lrc;           /* .lrc 路径 / netease:... / qq:... */
    gint     lrc_offset_ms; /* 正数表示歌词延后（毫秒） */
    gchar   *page;          /* 平台页面链接（二维码 / 徽标用） */
    gchar   *note;
    gchar   *platform;
    gboolean link_remote;
    gboolean pic_remote;
    gboolean lrc_remote;
} AudioTrack;

typedef enum {
    REPEAT_OFF = 0,
    REPEAT_ALL,
    REPEAT_ONE
} RepeatMode;

typedef struct {
    gint     width;
    gint     height;
    gboolean fullscreen;
    gchar   *title;
    gchar   *qrcode_link;
    gchar   *qrcode_label;
    gchar   *note;

    gint     repeat;
    gboolean shuffle;

    /* 缓存目录；NULL = <用户缓存目录>/follow */
    gchar   *cache_dir;

    gint     resolve_ttl;      /* 解析缓存 TTL（秒，0 = 不缓存） */
    gboolean audio_cache;
    gint     audio_max_mb;     /* 上限（MB） */
    gint     cover_max_mb;     /* 上限（MB） */

    /* 粒子效果：off / dust / warp / flow */
    gchar   *viz_effect;
    gint     viz_particles;   /* 0 = 默认 */
    double   viz_gain;        /* 1.0 为基准 */

    gboolean animations;
    gint     backdrop_fps;    /* 10..240 */

    /* 原始绑定；NULL = 内置默认 */
    gchar   *key_bind[KEY_ACTION_COUNT];

    gchar   *user_agent;
    gchar   *proxy;             /* 未配置时直连 */
    gint     http_timeout;      /* 请求总超时（秒） */
    gint     connect_timeout;   /* 连接超时（秒） */

    gboolean metadata_enabled;
    gboolean metadata_cover;
    gchar   *metadata_db;       /* NULL = <cache>/follow/metadata.db */
    gchar   *metadata_user_agent;

    AudioTrack *tracks;
    gsize       n_tracks;
} APPconfig;

/* 加载配置；缺失项使用默认值。返回进程内单例，不得释放。 */
const APPconfig *config_load(const char *path);

/* 获取配置；未加载时返回默认配置。 */
const APPconfig *config_get(void);

/* 释放配置；可重复调用。 */
void config_free(void);

/* 备注：曲目 note 优先于全局 note。 */
const gchar *config_track_note(const APPconfig *cfg, const AudioTrack *track);

const gchar *config_repeat_name(gint repeat);

/* 取第 index 个曲目；越界返回 NULL。 */
const AudioTrack *config_track_at(const APPconfig *cfg, gsize index);

/* 显示标题；返回新分配的字符串。 */
gchar *config_track_title(const AudioTrack *track);

/* 返回新分配的 "艺人 - 曲名"。 */
gchar *config_track_label(const AudioTrack *track);

/* 是否为 http/https 地址。 */
gboolean config_is_remote(const gchar *uri);

/* 网络参数：未配置时返回内置默认值。 */
const gchar *config_user_agent(void);
gint         config_http_timeout(void);
gint         config_http_connect_timeout(void);

/* proxy；未配置返回 NULL。 */
const gchar *config_proxy(void);

/* 元数据配置。 */
gboolean     config_metadata_enabled(void);
gboolean     config_metadata_cover(void);
const gchar *config_metadata_user_agent(void);
gchar       *config_metadata_db_path(void);

/* 粒子参数：返回经过范围限制的值。 */
const gchar *config_viz_effect(void);
gint         config_viz_particles(void);
double       config_viz_gain(void);

const gchar *config_key_binding(KeyAction action);

/* 解析 TTL（秒）；音频 / 封面缓存上限（字节）。 */
gint    config_resolve_ttl(void);
gboolean config_audio_cache(void);
guint64 config_audio_max_bytes(void);
guint64 config_cover_max_bytes(void);

/* 动效开关 */
gboolean config_animations(void);
void     config_set_animations(gboolean on);

/* 动态背景刷新率（帧/秒）。 */
gint     config_backdrop_fps(void);

/* 转换为文件系统路径；返回新分配的字符串。 */
gchar *config_to_local_path(const gchar *uri);

#endif
