/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 免责声明：作者不提供、不托管任何音视频内容，不保证第三方接口的可用性与合法性；
 * 内容版权归各自权利人所有，使用者须自行遵守所在地法律与各平台服务条款。
 */

#include "appicon.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

#ifndef FOLLOW_APP_ID
#define FOLLOW_APP_ID "org.github.clevebitr.follow"
#endif

#define APPICON_PATH   "src/icons/logo-mark.svg"
#define APPICON_FALLBACK "src/icons/logo.svg"
#define APPICON_PX     256

static GdkTexture *g_icon;

static GdkTexture *load_icon(void)
{
    g_autoptr(GError) error = NULL;
    GdkPixbuf *pixbuf;
    GdkTexture *texture;

    pixbuf = gdk_pixbuf_new_from_file_at_scale(APPICON_PATH, APPICON_PX,
                                               APPICON_PX, TRUE, &error);
    if (pixbuf == NULL) {
        g_clear_error(&error);
        pixbuf = gdk_pixbuf_new_from_file_at_scale(APPICON_FALLBACK, APPICON_PX,
                                                   APPICON_PX, TRUE, &error);
    }

    if (pixbuf == NULL) {
        g_debug("appicon: cannot load %s: %s", APPICON_PATH,
                error != NULL ? error->message : "unknown error");
        return NULL;
    }

    texture = gdk_texture_new_for_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return texture;
}

void appicon_apply(GtkWidget *window)
{
    GdkSurface *surface;
    GList      *icons;

    if (window == NULL)
        return;

    gtk_window_set_default_icon_name(FOLLOW_APP_ID);
    gtk_window_set_icon_name(GTK_WINDOW(window), FOLLOW_APP_ID);

    if (g_icon == NULL)
        g_icon = load_icon();

    if (g_icon == NULL)
        return;

    surface = gtk_native_get_surface(gtk_widget_get_native(window));
    if (surface == NULL || !GDK_IS_TOPLEVEL(surface))
        return;

    icons = g_list_append(NULL, g_icon);
    gdk_toplevel_set_icon_list(GDK_TOPLEVEL(surface), icons);
    g_list_free(icons);
}

void appicon_clear(void)
{
    g_clear_object(&g_icon);
}
