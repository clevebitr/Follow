/**
 * created: 2026/11/15
 * author: clevebitr
 * github: https://github.com/clevebitr
 * last modified: 2026/11/15
 *
 * 免责声明：本项目仅供学习与技术研究使用。作者不提供、不托管任何音视频内容，
 * 不保证第三方接口的可用性与合法性；内容版权归各自权利人所有。
 * 使用者须自行遵守所在地法律与各平台服务条款，使用后果自负。
 */

#ifndef FOLLOW_CORE_PUBLIC_H
#define FOLLOW_CORE_PUBLIC_H

/* follow::core 的公开 API。
 *
 * 这一层只依赖 GLib / GTK 基础库，不知道「音乐」「网络」「界面」的存在：
 *   config    读取 src/config.ini，进程内单例
 *   cache     统一磁盘缓存目录（歌词 / 封面 / 元数据 / 统计）
 *   settings  运行时设置（音量 / 静音 / 输出设备）
 *   keymap    按键绑定
 *   stats     收听统计
 *   palette   封面取色
 *   texture   cairo surface → GL 纹理
 *   platform  曲目来源平台识别 + 品牌色
 *
 * 其它模块通过 target_link_libraries(... follow::core) 拿到这些头文件。 */

#include "cache.h"
#include "config.h"
#include "keymap.h"
#include "palette.h"
#include "platform.h"
#include "settings.h"
#include "stats.h"
#include "texture.h"

#endif /* FOLLOW_CORE_PUBLIC_H */
