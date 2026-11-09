#ifndef FOLLOW_HELP_H
#define FOLLOW_HELP_H

#include <cairo.h>

/* F1：覆盖整屏的快捷键页。键盘是唯一的操作方式，所以这一页必须好读。 */
void help_draw(cairo_t *cr, double width, double height);

#endif /* FOLLOW_HELP_H */
