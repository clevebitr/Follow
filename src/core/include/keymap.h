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

#ifndef FOLLOW_KEYMAP_H
#define FOLLOW_KEYMAP_H

#include <gdk/gdk.h>
#include <glib.h>

/* 按键绑定：[keys] 组 → 动作。多键用逗号分隔，off / none 解绑。 */

typedef enum {
    KEY_PLAY_PAUSE = 0,
    KEY_SEEK_BACK,
    KEY_SEEK_FORWARD,
    KEY_NEXT,
    KEY_PREV,
    KEY_QUEUE_PREV,
    KEY_QUEUE_NEXT,
    KEY_RESTART,
    KEY_STOP,
    KEY_REPEAT,
    KEY_SHUFFLE,
    KEY_LYRIC_EARLIER,
    KEY_LYRIC_LATER,
    KEY_LYRIC_RESET,
    KEY_FULLSCREEN,
    KEY_HELP,
    KEY_PLAYLIST,
    KEY_BACK,
    KEY_QUIT,
    KEY_PERF,
    KEY_DETAILS,
    KEY_ACTION_COUNT
} KeyAction;

/* 解析 [keys] 里的键名（大小写不敏感，支持少量别名）；不认识返回 -1。 */
gint keymap_action_from_name(const gchar *name);

/* 从 config 读出的原始值构建绑定表；启动时调一次。 */
void keymap_init(void);

/* 释放 label 缓存，退出时调一次。 */
void keymap_clear(void);

/* 未显式写 shift 时不要求 Shift；写了 shift+ 则必须按住。 */
gboolean keymap_match(KeyAction action, guint keyval, GdkModifierType state);

/* 显示用按键名；返回的字符串由 keymap 持有。 */
const gchar *keymap_label(KeyAction action);

#endif
