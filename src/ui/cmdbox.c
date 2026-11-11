/**
 * created: 2026/11/11
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/11
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */
#include "cmdbox.h"
#include <string.h>
#include "config.h"
#include "lyricview.h"
#include "media.h"
#include "player.h"
#include "playlist.h"
#include "settings.h"
#include "ui.h"
#include "viz.h"
typedef enum {
    ACT_RELOAD = 0,
    ACT_VOLUME_MENU,
    ACT_OUTPUT_MENU,
    ACT_VIZ_MENU,
    ACT_ANIM_TOGGLE,
    ACT_REPEAT_CYCLE,
    ACT_SHUFFLE_TOGGLE,
    ACT_LYRIC_EARLIER,
    ACT_LYRIC_LATER,
    ACT_LYRIC_RESET,
    ACT_WALL,
    ACT_HELP,
    ACT_FULLSCREEN,
    ACT_QUIT,
    ACT_VOLUME_UP,
    ACT_VOLUME_DOWN,
    ACT_VOLUME_HALF,
    ACT_VOLUME_FULL,
    ACT_MUTE,
    ACT_OUTPUT_SET,
    ACT_VIZ_SET,
    ACT_BACK
} PaletteAction;
typedef struct {
    gchar        *title;
    gchar        *detail;
    PaletteAction action;
    gchar        *arg;
} PaletteItem;
typedef enum {
    MENU_ROOT = 0,
    MENU_VOLUME,
    MENU_OUTPUT,
    MENU_VIZ
} PaletteMenu;
static struct {
    GtkWidget  *box;
    GtkWidget  *list;
    GtkWidget  *entry;
    GtkWidget  *message;
    GPtrArray  *items;
    PaletteMenu menu;
    int         selected;
    int         shown;
    gboolean    open;
    gboolean    updating;
} P;
/* ---------------- 工具 ---------------- */
static void set_message(const gchar *text)
{
    if (P.message == NULL)
        return;
    if (text != NULL && *text != '\0') {
        gtk_label_set_text(GTK_LABEL(P.message), text);
        gtk_widget_set_visible(P.message, TRUE);
    } else {
        gtk_widget_set_visible(P.message, FALSE);
    }
}
static void clear_rows(void)
{
    GtkListBoxRow *row;
    if (P.list == NULL)
        return;
    while ((row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(P.list), 0)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(P.list), GTK_WIDGET(row));
    P.shown    = 0;
    P.selected = -1;
}
static void clear_items(void)
{
    if (P.items == NULL)
        return;
    for (guint i = 0; i < P.items->len; i++) {
        PaletteItem *item = g_ptr_array_index(P.items, i);
        g_free(item->title);
        g_free(item->detail);
        g_free(item->arg);
        g_free(item);
    }
    g_ptr_array_set_size(P.items, 0);
}
static void add_item(const gchar *title, const gchar *detail,
                     PaletteAction action, const gchar *arg)
{
    PaletteItem *item = g_new0(PaletteItem, 1);
    item->title  = g_strdup(title != NULL ? title : "");
    item->detail = detail != NULL ? g_strdup(detail) : NULL;
    item->action = action;
    item->arg    = arg != NULL ? g_strdup(arg) : NULL;
    g_ptr_array_add(P.items, item);
}
static gboolean fuzzy_match(const gchar *query, const gchar *text)
{
    const gchar *q;
    const gchar *t;
    if (query == NULL || *query == '\0')
        return TRUE;
    if (text == NULL)
        return FALSE;
    q = query;
    t = text;
    while (*q != '\0') {
        gchar c = g_ascii_tolower(*q++);
        if (c == ' ')
            continue;
        while (*t != '\0' && g_ascii_tolower(*t) != c)
            t++;
        if (*t == '\0')
            return FALSE;
        t++;
    }
    return TRUE;
}
static gchar *volume_detail(void)
{
    if (settings_muted())
        return g_strdup("muted");
    return g_strdup_printf("%.0f%%", settings_volume() * 100.0);
}
/* ---------------- 列表 ---------------- */
static void select_index(int index)
{
    GtkListBoxRow *row;
    if (P.shown <= 0) {
        P.selected = -1;
        gtk_list_box_unselect_all(GTK_LIST_BOX(P.list));
        return;
    }
    if (index < 0)
        index = P.shown - 1;
    if (index >= P.shown)
        index = 0;
    P.selected = index;
    row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(P.list), index);
    if (row != NULL)
        gtk_list_box_select_row(GTK_LIST_BOX(P.list), row);
}
static void add_row(PaletteItem *item)
{
    GtkWidget *row   = gtk_list_box_row_new();
    GtkWidget *line  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title = gtk_label_new(item->title);
    gtk_widget_add_css_class(line, "command-row");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_add_css_class(title, "command-title");
    gtk_box_append(GTK_BOX(line), title);
    if (item->detail != NULL && *item->detail != '\0') {
        GtkWidget *detail = gtk_label_new(item->detail);
        gtk_label_set_xalign(GTK_LABEL(detail), 1.0);
        gtk_label_set_ellipsize(GTK_LABEL(detail), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(detail, "command-detail");
        gtk_box_append(GTK_BOX(line), detail);
    }
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), line);
    gtk_widget_set_can_focus(row, FALSE);
    g_object_set_data(G_OBJECT(row), "palette-item", item);
    gtk_list_box_append(GTK_LIST_BOX(P.list), row);
    P.shown++;
}
static void add_empty_row(void)
{
    GtkWidget *row  = gtk_list_box_row_new();
    GtkWidget *text = gtk_label_new("没有匹配的命令");
    gtk_widget_add_css_class(text, "command-detail");
    gtk_widget_set_halign(text, GTK_ALIGN_START);
    gtk_widget_set_margin_start(text, 8);
    gtk_widget_set_margin_end(text, 8);
    gtk_widget_set_margin_top(text, 6);
    gtk_widget_set_margin_bottom(text, 6);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), text);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_append(GTK_LIST_BOX(P.list), row);
}
static void rebuild_rows(void)
{
    const gchar *query;
    if (P.entry == NULL || P.list == NULL)
        return;
    query = gtk_editable_get_text(GTK_EDITABLE(P.entry));
    clear_rows();
    for (guint i = 0; i < P.items->len; i++) {
        PaletteItem *item = g_ptr_array_index(P.items, i);
        if (*query != '\0' &&
            !fuzzy_match(query, item->title) &&
            !fuzzy_match(query, item->detail))
            continue;
        add_row(item);
    }
    if (P.shown <= 0)
        add_empty_row();
    select_index(0);
}
static void set_query(const gchar *text)
{
    if (P.entry == NULL)
        return;
    P.updating = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(P.entry), text != NULL ? text : "");
    P.updating = FALSE;
}
/* ---------------- 菜单 ---------------- */
static void palette_show_root(gboolean clear_query);
static void palette_show_volume(gboolean clear_query);
static void palette_show_output(gboolean clear_query);
static void palette_show_viz(gboolean clear_query);
static void begin_menu(PaletteMenu menu, const gchar *placeholder, gboolean clear_query)
{
    clear_rows();
    clear_items();
    set_message(NULL);
    P.menu = menu;
    gtk_entry_set_placeholder_text(GTK_ENTRY(P.entry), placeholder);
    if (clear_query)
        set_query("");
}
static void palette_show_root(gboolean clear_query)
{
    g_autofree gchar *vol  = volume_detail();
    g_autofree gchar *out  = g_strdup(media_output_device_name(&app.media));
    g_autofree gchar *anim = g_strdup(config_animations() ? "on" : "off");
    g_autofree gchar *rep  = g_strdup(config_repeat_name(app.repeat));
    g_autofree gchar *shuf = g_strdup(app.shuffle ? "on" : "off");
    g_autofree gchar *lrc  = g_strdup_printf("%+d ms", app.lrc_offset_ms);
    begin_menu(MENU_ROOT, "输入命令，Enter 执行，Esc 关闭", clear_query);
    add_item("Reload config.ini", "重新读取 src/config.ini", ACT_RELOAD, NULL);
    add_item("Audio volume...", vol, ACT_VOLUME_MENU, NULL);
    add_item("Audio output...", out, ACT_OUTPUT_MENU, NULL);
    add_item("Visualizer...", viz_effect_name(), ACT_VIZ_MENU, NULL);
    add_item("Toggle animations", anim, ACT_ANIM_TOGGLE, NULL);
    add_item("Cycle repeat mode", rep, ACT_REPEAT_CYCLE, NULL);
    add_item("Toggle shuffle", shuf, ACT_SHUFFLE_TOGGLE, NULL);
    add_item("Lyrics offset +250 ms", lrc, ACT_LYRIC_LATER, NULL);
    add_item("Lyrics offset -250 ms", lrc, ACT_LYRIC_EARLIER, NULL);
    add_item("Reset lyric offset", lrc, ACT_LYRIC_RESET, NULL);
    add_item("Toggle song wall", NULL, ACT_WALL, NULL);
    add_item("Toggle help", NULL, ACT_HELP, NULL);
    add_item("Toggle fullscreen", NULL, ACT_FULLSCREEN, NULL);
    add_item("Quit", "关闭 Follow", ACT_QUIT, NULL);
    rebuild_rows();
}
static void palette_show_volume(gboolean clear_query)
{
    g_autofree gchar *vol = volume_detail();
    begin_menu(MENU_VOLUME, "调整音量…", clear_query);
    add_item("Volume +5%", vol, ACT_VOLUME_UP, NULL);
    add_item("Volume -5%", vol, ACT_VOLUME_DOWN, NULL);
    add_item("Volume 50%", vol, ACT_VOLUME_HALF, NULL);
    add_item("Volume 100%", vol, ACT_VOLUME_FULL, NULL);
    add_item("Toggle mute", vol, ACT_MUTE, NULL);
    add_item("Back", "返回", ACT_BACK, NULL);
    rebuild_rows();
}
static void palette_show_output(gboolean clear_query)
{
    GPtrArray    *names   = g_ptr_array_new_with_free_func(g_free);
    const gchar  *current = media_output_device_name(&app.media);
    const gchar  *saved   = settings_output_device();
    begin_menu(MENU_OUTPUT, "选择音频输出设备…", clear_query);
    add_item("System default",
             saved == NULL ? "current" : NULL,
             ACT_OUTPUT_SET, NULL);
    media_output_devices(&app.media, names);
    for (guint i = 0; i < names->len; i++) {
        const gchar *name   = g_ptr_array_index(names, i);
        const gchar *detail = NULL;
        if (saved != NULL && g_strcmp0(saved, name) == 0)
            detail = "current";
        else if (g_strcmp0(current, name) == 0)
            detail = "active";
        add_item(name, detail, ACT_OUTPUT_SET, name);
    }
    add_item("Back", "返回", ACT_BACK, NULL);
    g_ptr_array_unref(names);
    rebuild_rows();
}
static void palette_show_viz(gboolean clear_query)
{
    static const gchar *effects[] = { "off", "dust", "warp", "flow" };
    const gchar *current = viz_effect_name();
    begin_menu(MENU_VIZ, "选择可视化效果…", clear_query);
    for (gsize i = 0; i < G_N_ELEMENTS(effects); i++) {
        const gchar *name = effects[i];
        add_item(name,
                 g_strcmp0(current, name) == 0 ? "current" : NULL,
                 ACT_VIZ_SET, name);
    }
    add_item("Back", "返回", ACT_BACK, NULL);
    rebuild_rows();
}
/* ---------------- 执行 ---------------- */
static void apply_volume(double volume, gboolean unmute)
{
    volume = CLAMP(volume, 0.0, 1.0);
    settings_set_volume(volume);
    if (unmute) {
        settings_set_muted(FALSE);
        media_set_muted(&app.media, FALSE);
    }
    media_set_volume(&app.media, volume);
}
static void run_item(PaletteItem *item);

static void activate_selected(void)
{
    if (P.selected < 0 || P.selected >= P.shown)
        return;
    {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(P.list), P.selected);
        if (row != NULL)
            run_item(g_object_get_data(G_OBJECT(row), "palette-item"));
    }
}
static void run_item(PaletteItem *item)
{
    if (item == NULL)
        return;
    switch (item->action) {
    case ACT_RELOAD:
        palette_close();
        ui_reload_config();
        break;
    case ACT_VOLUME_MENU:
        palette_show_volume(TRUE);
        break;
    case ACT_OUTPUT_MENU:
        palette_show_output(TRUE);
        break;
    case ACT_VIZ_MENU:
        palette_show_viz(TRUE);
        break;
    case ACT_VOLUME_UP:
        apply_volume(settings_volume() + 0.05, TRUE);
        palette_show_volume(FALSE);
        break;
    case ACT_VOLUME_DOWN:
        apply_volume(settings_volume() - 0.05, TRUE);
        palette_show_volume(FALSE);
        break;
    case ACT_VOLUME_HALF:
        apply_volume(0.50, TRUE);
        palette_show_volume(FALSE);
        break;
    case ACT_VOLUME_FULL:
        apply_volume(1.0, TRUE);
        palette_show_volume(FALSE);
        break;
    case ACT_MUTE: {
        gboolean muted = !settings_muted();
        settings_set_muted(muted);
        media_set_muted(&app.media, muted);
        palette_show_volume(FALSE);
        break;
    }
    case ACT_OUTPUT_SET: {
        int result = media_set_output_device(&app.media, item->arg);

        if (result == 0) {
            settings_set_output_device(item->arg);
            palette_close();
        } else if (result == 1) {
            palette_show_output(FALSE);
            set_message("该设备不可用，已回退到原输出");
        } else {
            set_message("无法切换到该输出设备");
        }
        break;
    }
    case ACT_VIZ_SET:
        viz_configure(item->arg, config_viz_particles(), config_viz_gain());
        palette_close();
        ui_request_redraw();
        ui_animate();
        break;
    case ACT_ANIM_TOGGLE:
        config_set_animations(!config_animations());
        palette_close();
        ui_request_redraw();
        break;
    case ACT_REPEAT_CYCLE:
        player_cycle_repeat();
        palette_close();
        break;
    case ACT_SHUFFLE_TOGGLE:
        player_toggle_shuffle();
        palette_close();
        break;
    case ACT_LYRIC_EARLIER:
        lyricview_shift_offset(-250);
        palette_close();
        break;
    case ACT_LYRIC_LATER:
        lyricview_shift_offset(+250);
        palette_close();
        break;
    case ACT_LYRIC_RESET:
        lyricview_reset_offset();
        palette_close();
        break;
    case ACT_WALL:
        palette_close();
        playlist_toggle();
        break;
    case ACT_HELP:
        app.help = !app.help;
        app.chrome_dirty = TRUE;
        palette_close();
        ui_request_redraw();
        break;
    case ACT_FULLSCREEN:
        palette_close();
        ui_toggle_fullscreen();
        break;
    case ACT_QUIT:
        palette_close();
        if (app.window != NULL)
            gtk_window_close(GTK_WINDOW(app.window));
        break;
    case ACT_BACK:
        palette_show_root(TRUE);
        break;
    default:
        break;
    }
}
/* ---------------- 事件 ---------------- */
static void on_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    PaletteItem *item;
    (void)list;
    (void)data;
    item = g_object_get_data(G_OBJECT(row), "palette-item");
    if (item != NULL)
        run_item(item);
}
static void on_entry_changed(GtkEditable *editable, gpointer data)
{
    (void)editable;
    (void)data;
    if (P.updating || !P.open)
        return;
    rebuild_rows();
}
static gboolean on_entry_key(GtkEventControllerKey *controller, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data)
{
    const gboolean ctrl_shift_p =
        (state & GDK_CONTROL_MASK) != 0 &&
        (state & GDK_SHIFT_MASK) != 0 &&
        (keyval == GDK_KEY_P || keyval == GDK_KEY_p);
    (void)controller;
    (void)keycode;
    (void)data;
    if (ctrl_shift_p) {
        palette_close();
        return TRUE;
    }
    switch (keyval) {
    case GDK_KEY_Escape:
        palette_close();
        return TRUE;
    case GDK_KEY_Up:
        select_index(P.selected - 1);
        return TRUE;
    case GDK_KEY_Down:
        select_index(P.selected + 1);
        return TRUE;
    case GDK_KEY_Page_Up:
        select_index(P.selected - 5);
        return TRUE;
    case GDK_KEY_Page_Down:
        select_index(P.selected + 5);
        return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        activate_selected();
        return TRUE;
    case GDK_KEY_BackSpace:
        if (P.menu != MENU_ROOT &&
            *gtk_editable_get_text(GTK_EDITABLE(P.entry)) == '\0') {
            palette_show_root(TRUE);
            return TRUE;
        }
        break;
    case GDK_KEY_Tab:
        return TRUE;
    default:
        break;
    }
    return FALSE;
}
/* ---------------- 对外接口 ---------------- */
GtkWidget *palette_new(void)
{
    P.items = g_ptr_array_new();
    P.box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(P.box, "command-palette");
    gtk_widget_set_halign(P.box, GTK_ALIGN_END);
    gtk_widget_set_valign(P.box, GTK_ALIGN_END);
    gtk_widget_set_hexpand(P.box, FALSE);
    gtk_widget_set_vexpand(P.box, FALSE);
    gtk_widget_set_margin_end(P.box, 28);
    gtk_widget_set_margin_bottom(P.box, 28);
    gtk_widget_set_size_request(P.box, 440, -1);
    gtk_widget_set_visible(P.box, FALSE);
    P.message = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(P.message), 0.0);
    gtk_widget_add_css_class(P.message, "command-message");
    gtk_widget_set_visible(P.message, FALSE);
    gtk_box_append(GTK_BOX(P.box), P.message);
    P.list = gtk_list_box_new();
    gtk_widget_add_css_class(P.list, "command-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(P.list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(P.list), TRUE);
    gtk_widget_set_can_focus(P.list, FALSE);
    g_signal_connect(P.list, "row-activated", G_CALLBACK(on_row_activated), NULL);
    gtk_box_append(GTK_BOX(P.box), P.list);
    P.entry = gtk_entry_new();
    gtk_widget_add_css_class(P.entry, "command-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(P.entry), "输入命令…");
    gtk_widget_set_hexpand(P.entry, TRUE);
    g_signal_connect(P.entry, "changed", G_CALLBACK(on_entry_changed), NULL);
    gtk_box_append(GTK_BOX(P.box), P.entry);
    {
        GtkEventController *keys = gtk_event_controller_key_new();
        gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
        g_signal_connect(keys, "key-pressed", G_CALLBACK(on_entry_key), NULL);
        gtk_widget_add_controller(P.entry, keys);
    }
    return P.box;
}
void palette_open(void)
{
    if (P.open || P.box == NULL)
        return;
    P.open = TRUE;
    palette_show_root(TRUE);
    gtk_widget_set_visible(P.box, TRUE);
    gtk_widget_grab_focus(P.entry);
}
void palette_open_query(const gchar *query)
{
    palette_open();
    if (query != NULL && *query != '\0') {
        set_query(query);
        rebuild_rows();
    }
}
void palette_close(void)
{
    if (!P.open)
        return;
    P.open = FALSE;
    gtk_widget_set_visible(P.box, FALSE);
    if (app.window != NULL)
        gtk_window_set_focus(GTK_WINDOW(app.window), NULL);
}
void palette_toggle(void)
{
    if (P.open)
        palette_close();
    else
        palette_open();
}
gboolean palette_is_open(void)
{
    return P.open;
}
void palette_debug_steps(const gchar *steps)
{
    g_auto(GStrv) parts = NULL;
    if (steps == NULL || *steps == '\0')
        return;
    parts = g_strsplit(steps, ",", -1);
    for (gsize i = 0; parts[i] != NULL; i++) {
        gchar *step = g_strstrip(parts[i]);
        if (g_ascii_strcasecmp(step, "enter") == 0) {
            activate_selected();
        } else if (g_ascii_strcasecmp(step, "down") == 0) {
            select_index(P.selected + 1);
        } else if (g_ascii_strcasecmp(step, "up") == 0) {
            select_index(P.selected - 1);
        } else if (g_ascii_strcasecmp(step, "esc") == 0) {
            palette_close();
            break;
        } else if (g_str_has_prefix(step, "text:")) {
            set_query(step + 5);
            rebuild_rows();
        }
    }
}

gboolean palette_contains(double x, double y)
{
    graphene_rect_t bounds;

    if (!P.open || P.box == NULL || !gtk_widget_get_visible(P.box))
        return FALSE;

    {
        GtkWidget *root = gtk_widget_get_parent(P.box);

        if (root == NULL || !gtk_widget_compute_bounds(P.box, root, &bounds))
            return FALSE;
    }

    return x >= bounds.origin.x &&
           x <= bounds.origin.x + bounds.size.width &&
           y >= bounds.origin.y &&
           y <= bounds.origin.y + bounds.size.height;
}
