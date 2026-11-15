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

#ifndef FOLLOW_KEYMAP_H
#define FOLLOW_KEYMAP_H

#include <gdk/gdk.h>
#include <glib.h>

/* 按键绑定：ini 的 [keys] 组 → 动作。
 *
 *   [keys]
 *   play_pause = space
 *   next       = n, Page_Down
 *   playlist   = Tab
 *   fullscreen = f, F11
 *
 * 值可以用逗号 / 竖线分隔多个键；空值或 off / none 表示解绑。
 * 键名用 GDK 的名字（space / Left / F11 / bracketleft / Page_Up ...），
 * 也接受 esc / enter / pgup / pgdn 这些别名。可以带 ctrl+ / alt+ / shift+ / super+。 */

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
    KEY_ACTION_COUNT
} KeyAction;

/* 解析 [keys] 里的键名（大小写不敏感，支持少量别名）；不认识返回 -1。 */
gint keymap_action_from_name(const gchar *name);

/* 从 config 读出的原始值构建绑定表；启动时调一次。 */
void keymap_init(void);

/* 释放 label 缓存，退出时调一次。 */
void keymap_clear(void);

/* 这个按键事件是否命中动作。没有写 shift 的绑定不要求 Shift 状态，
 * 所以 N / n 都能用；写了 shift+ 的绑定则必须按住 Shift。 */
gboolean keymap_match(KeyAction action, guint keyval, GdkModifierType state);

/* 显示用的按键名，如 "SPACE" / "← / →" / "F / F11"。
 * 没绑定返回 "—"；返回的字符串由 keymap 持有，不要 free。 */
const gchar *keymap_label(KeyAction action);

#endif /* FOLLOW_KEYMAP_H */
