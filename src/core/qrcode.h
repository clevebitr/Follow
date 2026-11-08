#ifndef QRCODE_H
#define QRCODE_H

#include <qrencode.h>
#include <gdk/gdk.h>



QRcode *qrcode_generate(const char *text);
GdkTexture *qrcode_to_texture(QRcode *code, int scale, int margin);

#endif