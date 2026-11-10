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

#ifndef FOLLOW_VIZ_H
#define FOLLOW_VIZ_H

#include <cairo.h>
#include <glib.h>

#include "palette.h"

/* 音频粒子背景（dust / warp / flow）。
 * 画在动态背景的小纹理里、scrim 之前；不需要 GL，也不依赖 libvisual。 */

void        viz_configure(const gchar *effect, int particles, double gain);
void        viz_reset(void);                 /* 退出时释放 */
gboolean    viz_active(void);
const gchar *viz_effect_name(void);

/* 每帧调用：读 media scope、做 FFT、推进粒子。 */
void        viz_tick(void);

/* 在自己的 cairo 目标上画粒子；pal 是当前封面取出的颜色。 */
void        viz_draw(cairo_t *cr, double w, double h, const Palette *pal);

/* 换曲时放一小撮新粒子，配合背景退潮过渡。 */
void        viz_burst(void);

#endif /* FOLLOW_VIZ_H */
