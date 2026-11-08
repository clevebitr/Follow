#include "qrcode.h"

QRcode *qrcode_generate(const char *text)
{
    if (!text) return NULL;
    return QRcode_encodeString(text, 0, QR_ECLEVEL_M, QR_MODE_8, 1);
}

GdkTexture *qrcode_to_texture(QRcode *code, int scale, int margin)
{
    if (!code || scale < 1 || margin < 0) return NULL;

    const int size  = code->width;            /* 二维码模块数 */
    const int total = (size + margin * 2) * scale; /* 像素边长 */

    GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, total, total);
    if (!pixbuf) return NULL;

    gdk_pixbuf_fill(pixbuf, 0x303446FF);

    guchar *pixels    = gdk_pixbuf_get_pixels(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const int nchan     = gdk_pixbuf_get_n_channels(pixbuf);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (!(code->data[y * size + x] & 1)) continue;

            const int px0 = (x + margin) * scale;
            const int py0 = (y + margin) * scale;
            for (int sy = 0; sy < scale; sy++) {
                guchar *row = pixels + (py0 + sy) * rowstride;
                for (int sx = 0; sx < scale; sx++) {
                    guchar *p = row + (px0 + sx) * nchan;
                    p[0] = p[1] = p[2] = 0xFF;
                }
            }
        }
    }

    /* GTK 4.20 起 gdk_texture_new_for_pixbuf() 已弃用，改用 memory texture */
    const gsize stride = (gsize)gdk_pixbuf_get_rowstride(pixbuf);
    GBytes *bytes = g_bytes_new(pixels, stride * (gsize)total);
    GdkTexture *texture =
        gdk_memory_texture_new(total, total, GDK_MEMORY_R8G8B8, bytes, stride);

    g_bytes_unref(bytes);
    g_object_unref(pixbuf);
    return texture;
}