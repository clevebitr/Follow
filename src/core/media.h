#ifndef MEDIA_H
#define MEDIA_H

#include "miniaudio.h"

typedef struct {
    ma_engine engine;
    ma_sound  sound;
    int       ready;     /* 1 = engine 已成功初始化 */
    int       has_sound; /* 1 = sound 已加载 */
} Media;

int media_init(Media *m, ma_context *ctx);

/* 加载并开始播放（会先停掉当前曲目） */
int media_play(Media *m, const char *path);

/* 暂停当前曲目 */
int media_pause(Media *m);

/* 继续播放当前曲目 */
int media_continue(Media *m);

/* 停止并卸载当前曲目 */
void media_stop(Media *m);

/* 当前播放位置 / 总时长，单位秒；没有曲目时返回 0 */
double media_position(const Media *m);
double media_duration(const Media *m);

/* 跳到指定秒数，成功返回 0 */
int  media_seek(Media *m, double seconds);

int  media_is_playing(const Media *m);
int  media_is_paused(const Media *m);


void media_clear(Media *m);

#endif
