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

#include "keymap.h"

#include <string.h>

#include "config.h"

#define KEYMAP_MAX_BINDS 8

typedef struct {
    guint           keyval;
    GdkModifierType mods;
} KeyBind;

static struct {
    KeyBind bind[KEYMAP_MAX_BINDS];
    int     n;
} g_map[KEY_ACTION_COUNT];

static gchar *g_label[KEY_ACTION_COUNT];

static const struct {
    const gchar *name;
    KeyAction    action;
} g_actions[] = {
    { "play_pause",   KEY_PLAY_PAUSE },
    { "play",         KEY_PLAY_PAUSE },
    { "toggle_play",  KEY_PLAY_PAUSE },
    { "seek_back",    KEY_SEEK_BACK },
    { "rewind",       KEY_SEEK_BACK },
    { "seek_forward", KEY_SEEK_FORWARD },
    { "forward",      KEY_SEEK_FORWARD },
    { "next",         KEY_NEXT },
    { "next_track",   KEY_NEXT },
    { "skip",         KEY_NEXT },
    { "prev",         KEY_PREV },
    { "previous",     KEY_PREV },
    { "prev_track",   KEY_PREV },
    { "restart",      KEY_RESTART },
    { "replay",       KEY_RESTART },
    { "stop",         KEY_STOP },
    { "repeat",       KEY_REPEAT },
    { "loop",         KEY_REPEAT },
    { "shuffle",      KEY_SHUFFLE },
    { "random",       KEY_SHUFFLE },
    { "lyric_earlier", KEY_LYRIC_EARLIER },
    { "lyrics_earlier", KEY_LYRIC_EARLIER },
    { "lrc_earlier",  KEY_LYRIC_EARLIER },
    { "lyric_later",  KEY_LYRIC_LATER },
    { "lyrics_later", KEY_LYRIC_LATER },
    { "lrc_later",    KEY_LYRIC_LATER },
    { "lyric_reset",  KEY_LYRIC_RESET },
    { "lyrics_reset", KEY_LYRIC_RESET },
    { "lrc_reset",    KEY_LYRIC_RESET },
    { "fullscreen",   KEY_FULLSCREEN },
    { "full_screen",  KEY_FULLSCREEN },
    { "fs",           KEY_FULLSCREEN },
    { "help",         KEY_HELP },
    { "shortcuts",    KEY_HELP },
    { "keybindings",  KEY_HELP },
    { "playlist",     KEY_PLAYLIST },
    { "queue",        KEY_PLAYLIST },
    { "list",         KEY_PLAYLIST },
    { "tracks",       KEY_PLAYLIST },
    { "back",         KEY_BACK },
    { "close",        KEY_BACK },
    { "cancel",       KEY_BACK },
    { "quit",         KEY_QUIT },
    { "exit",         KEY_QUIT },
};

/* 没写 [keys] 时的默认值，和 README / config.ini 保持一致 */
static const gchar *g_defaults[KEY_ACTION_COUNT] = {
    [KEY_PLAY_PAUSE]   = "space",
    [KEY_SEEK_BACK]    = "Left",
    [KEY_SEEK_FORWARD] = "Right",
    [KEY_NEXT]         = "n",
    [KEY_PREV]         = "p",
    [KEY_RESTART]      = "r",
    [KEY_STOP]         = "s",
    [KEY_REPEAT]       = "l",
    [KEY_SHUFFLE]      = "h",
    [KEY_LYRIC_EARLIER] = "bracketleft",
    [KEY_LYRIC_LATER]   = "bracketright",
    [KEY_LYRIC_RESET]   = "backslash",
    [KEY_FULLSCREEN]   = "f, F11",
    [KEY_HELP]         = "F1",
    [KEY_PLAYLIST]     = "Tab",
    [KEY_BACK]         = "Escape",
    [KEY_QUIT]         = "q",
};

gint keymap_action_from_name(const gchar *name)
{
    if (name == NULL || *name == '\0')
        return -1;

    for (gsize i = 0; i < G_N_ELEMENTS(g_actions); i++) {
        if (g_ascii_strcasecmp(name, g_actions[i].name) == 0)
            return (gint)g_actions[i].action;
    }

    return -1;
}

static const gchar *key_alias(const gchar *name)
{
    static const struct {
        const gchar *alias;
        const gchar *gdk;
    } aliases[] = {
        { "esc",      "Escape" },
        { "escape",   "Escape" },
        { "enter",    "Return" },
        { "return",   "Return" },
        { "pgup",     "Page_Up" },
        { "pageup",   "Page_Up" },
        { "pgdn",     "Page_Down" },
        { "pagedown", "Page_Down" },
        { "del",      "Delete" },
        { "delete",   "Delete" },
        { "ins",      "Insert" },
        { "insert",   "Insert" },
        { "plus",     "plus" },
        { "minus",    "minus" },
        { "comma",    "comma" },
        { "period",   "period" },
        { "slash",    "slash" },
        { "backslash", "backslash" },
        { "bracketleft",  "bracketleft" },
        { "bracketright", "bracketright" },
        { "spacebar", "space" },
        { "win",      "Super_L" },
        { "super",    "Super_L" },
        { "cmd",      "Super_L" },
        { "cmdorctrl", "Control_L" },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(aliases); i++) {
        if (g_ascii_strcasecmp(name, aliases[i].alias) == 0)
            return aliases[i].gdk;
    }

    return name;
}

static guint parse_keyval(const gchar *name)
{
    const gchar *gdk_name = key_alias(name);
    guint        keyval   = gdk_keyval_from_name(gdk_name);

    if (keyval != GDK_KEY_VoidSymbol)
        return keyval;

    /* GDK 的名字表只认 "bracketleft" 这类全名；单字符直接按 Unicode 认，
     * 这样 `[`、`]`、`\`、`;` 也能直接写。 */
    if (g_utf8_strlen(name, -1) == 1) {
        gunichar ch = g_utf8_get_char(name);

        if (ch < 128 && g_ascii_isalpha((gchar)ch))
            ch = (gunichar)g_ascii_tolower((gchar)ch);

        return gdk_unicode_to_keyval(ch);
    }

    return GDK_KEY_VoidSymbol;
}

static GdkModifierType parse_modifier(const gchar *name)
{
    if (g_ascii_strcasecmp(name, "ctrl") == 0 ||
        g_ascii_strcasecmp(name, "control") == 0)
        return GDK_CONTROL_MASK;
    if (g_ascii_strcasecmp(name, "alt") == 0 ||
        g_ascii_strcasecmp(name, "option") == 0)
        return GDK_ALT_MASK;
    if (g_ascii_strcasecmp(name, "shift") == 0)
        return GDK_SHIFT_MASK;
    if (g_ascii_strcasecmp(name, "super") == 0 ||
        g_ascii_strcasecmp(name, "win") == 0 ||
        g_ascii_strcasecmp(name, "cmd") == 0)
        return GDK_SUPER_MASK;

    return 0;
}

/* "ctrl+shift+Left" → { Left, CONTROL|SHIFT } */
static gboolean parse_binding(const gchar *token, KeyBind *out)
{
    g_auto(GStrv) parts = g_strsplit(token, "+", -1);
    gsize          n     = g_strv_length(parts);
    GdkModifierType mods = 0;
    const gchar    *keyname;

    if (n == 0 || parts[0] == NULL || *parts[0] == '\0')
        return FALSE;

    /* 至少最后一段是键；前面的全是修饰键 */
    for (gsize i = 0; i + 1 < n; i++) {
        GdkModifierType mod = parse_modifier(g_strstrip(parts[i]));

        if (mod == 0)
            return FALSE;
        mods |= mod;
    }

    keyname = g_strstrip(parts[n - 1]);
    if (*keyname == '\0')
        return FALSE;

    out->keyval = parse_keyval(keyname);
    out->mods   = mods;

    return out->keyval != GDK_KEY_VoidSymbol;
}

static void parse_action(KeyAction action, const gchar *raw)
{
    g_auto(GStrv) tokens = NULL;

    g_map[action].n = 0;

    if (raw == NULL)
        return;

    if (*raw == '\0' || g_ascii_strcasecmp(raw, "off") == 0 ||
        g_ascii_strcasecmp(raw, "none") == 0)
        return;      /* 显式解绑 */

    tokens = g_strsplit_set(raw, ",|", -1);

    for (gsize i = 0; tokens[i] != NULL && g_map[action].n < KEYMAP_MAX_BINDS; i++) {
        gchar *token = g_strstrip(tokens[i]);
        KeyBind bind;

        if (*token == '\0')
            continue;

        if (parse_binding(token, &bind))
            g_map[action].bind[g_map[action].n++] = bind;
        else
            g_warning("keys: cannot parse \"%s\" for action %d", token, (int)action);
    }
}

static gchar *keyval_label(guint keyval)
{
    switch (keyval) {
    case GDK_KEY_space:       return g_strdup("SPACE");
    case GDK_KEY_Left:        return g_strdup("←");
    case GDK_KEY_Right:       return g_strdup("→");
    case GDK_KEY_Up:          return g_strdup("↑");
    case GDK_KEY_Down:        return g_strdup("↓");
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:    return g_strdup("ENTER");
    case GDK_KEY_Escape:      return g_strdup("ESC");
    case GDK_KEY_Tab:         return g_strdup("TAB");
    case GDK_KEY_BackSpace:   return g_strdup("BKSP");
    case GDK_KEY_Delete:      return g_strdup("DEL");
    case GDK_KEY_Insert:      return g_strdup("INS");
    case GDK_KEY_Page_Up:     return g_strdup("PGUP");
    case GDK_KEY_Page_Down:   return g_strdup("PGDN");
    case GDK_KEY_Home:        return g_strdup("HOME");
    case GDK_KEY_End:         return g_strdup("END");
    case GDK_KEY_bracketleft: return g_strdup("[");
    case GDK_KEY_bracketright:return g_strdup("]");
    case GDK_KEY_backslash:   return g_strdup("\\");
    case GDK_KEY_comma:       return g_strdup(",");
    case GDK_KEY_period:      return g_strdup(".");
    case GDK_KEY_slash:       return g_strdup("/");
    case GDK_KEY_semicolon:   return g_strdup(";");
    case GDK_KEY_apostrophe:  return g_strdup("'");
    case GDK_KEY_grave:       return g_strdup("`");
    case GDK_KEY_minus:       return g_strdup("-");
    case GDK_KEY_equal:       return g_strdup("=");
    case GDK_KEY_plus:        return g_strdup("+");
    default:                  break;
    }

    {
        const gchar *name = gdk_keyval_name(keyval);

        if (name != NULL && *name != '\0') {
            gchar *out = g_strdup(name);

            for (gchar *p = out; *p != '\0'; p++)
                *p = g_ascii_toupper(*p);

            return out;
        }
    }

    return g_strdup("?");
}

static void build_labels(void)
{
    for (int a = 0; a < KEY_ACTION_COUNT; a++) {
        GString *label = g_string_new(NULL);

        for (int i = 0; i < g_map[a].n; i++) {
            g_autofree gchar *key = keyval_label(g_map[a].bind[i].keyval);

            if (label->len > 0)
                g_string_append(label, " / ");

            if (g_map[a].bind[i].mods & GDK_CONTROL_MASK)
                g_string_append(label, "CTRL+");
            if (g_map[a].bind[i].mods & GDK_ALT_MASK)
                g_string_append(label, "ALT+");
            if (g_map[a].bind[i].mods & GDK_SHIFT_MASK)
                g_string_append(label, "SHIFT+");
            if (g_map[a].bind[i].mods & GDK_SUPER_MASK)
                g_string_append(label, "SUPER+");

            g_string_append(label, key);
        }

        g_free(g_label[a]);
        g_label[a] = label->len > 0 ? g_string_free(label, FALSE) : g_string_free(label, TRUE);

        if (g_label[a] == NULL)
            g_label[a] = g_strdup("—");
    }
}

void keymap_init(void)
{
    const APPconfig *cfg = config_get();

    for (int a = 0; a < KEY_ACTION_COUNT; a++) {
        const gchar *raw = cfg->key_bind[a];
        int          before;

        if (raw == NULL) {
            parse_action((KeyAction)a, g_defaults[a]);
            continue;
        }

        parse_action((KeyAction)a, raw);
        before = g_map[a].n;

        /* 写了但一个都解析不出来：回落到默认值，避免用户把自己锁在门外 */
        if (before == 0 && *raw != '\0' &&
            g_ascii_strcasecmp(raw, "off") != 0 &&
            g_ascii_strcasecmp(raw, "none") != 0) {
            g_warning("keys: no usable binding for action %d, using default", a);
            parse_action((KeyAction)a, g_defaults[a]);
        }
    }

    build_labels();
}

void keymap_clear(void)
{
    for (int a = 0; a < KEY_ACTION_COUNT; a++) {
        g_clear_pointer(&g_label[a], g_free);
        g_map[a].n = 0;
    }
}

gboolean keymap_match(KeyAction action, guint keyval, GdkModifierType state)
{
    guint relevant = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK;
    guint have     = (guint)state & relevant;

    if (action < 0 || action >= KEY_ACTION_COUNT)
        return FALSE;

    for (int i = 0; i < g_map[action].n; i++) {
        guint           want = (guint)g_map[action].bind[i].mods & relevant;
        guint           bind = g_map[action].bind[i].keyval;
        GdkModifierType hard = GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK;

        if (bind != keyval && bind != gdk_keyval_to_lower(keyval) &&
            gdk_keyval_to_lower(bind) != gdk_keyval_to_lower(keyval))
            continue;

        if ((have & (guint)hard) != (want & (guint)hard))
            continue;

        if ((want & GDK_SHIFT_MASK) != 0 && (have & GDK_SHIFT_MASK) == 0)
            continue;

        return TRUE;
    }

    return FALSE;
}

const gchar *keymap_label(KeyAction action)
{
    if (action < 0 || action >= KEY_ACTION_COUNT || g_label[action] == NULL)
        return "—";

    return g_label[action];
}
