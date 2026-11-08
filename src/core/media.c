#define MINIAUDIO_IMPLEMENTATION
#include <stdio.h>
#include "media.h"

static void media_unload(Media *m)
{
    if (!m->has_sound) return;

    ma_sound_stop(&m->sound);
    ma_sound_uninit(&m->sound);
    m->has_sound = 0;
}

int media_init(Media *m, ma_context *ctx)
{
    if (m == NULL) return -1;
    if (m->ready) return 0;

    ma_engine_config config = ma_engine_config_init();
    config.pContext = ctx;

    ma_result result = ma_engine_init(&config, &m->engine);
    if (result != MA_SUCCESS) {
        printf("media: engine init failed: %s\n", ma_result_description(result));
        m->ready = 0;
        return -2;
    }

    m->ready = 1;
    return 0;
}

int media_play(Media *m, const char *path)
{
    ma_result result;

    if (m == NULL || !m->ready || path == NULL) return -1;

    media_unload(m);

    result = ma_sound_init_from_file(&m->engine, path, MA_SOUND_FLAG_STREAM,
                                     NULL, NULL, &m->sound);
    if (result != MA_SUCCESS) {
        printf("media: load '%s' failed: %s\n", path, ma_result_description(result));
        return -2;
    }
    m->has_sound = 1;

    result = ma_sound_start(&m->sound);
    if (result != MA_SUCCESS) {
        printf("media: start failed: %s\n", ma_result_description(result));
        media_unload(m);
        return -3;
    }

    return 0;
}

int media_pause(Media *m)
{
    if (m == NULL || !m->has_sound) return -1;
    return ma_sound_stop(&m->sound);
}

int media_continue(Media *m)
{
    if (m == NULL || !m->has_sound) return -1;
    return ma_sound_start(&m->sound);
}

void media_stop(Media *m)
{
    if (m == NULL) return;
    media_unload(m);
}

double media_position(const Media *m)
{
    float cursor = 0.0f;

    if (m == NULL || !m->has_sound) return 0.0;
    if (ma_sound_get_cursor_in_seconds(&m->sound, &cursor) != MA_SUCCESS)
        return 0.0;

    return (double)cursor;
}

double media_duration(const Media *m)
{
    float length = 0.0f;

    if (m == NULL || !m->has_sound) return 0.0;
    if (ma_sound_get_length_in_seconds(&m->sound, &length) != MA_SUCCESS)
        return 0.0;

    return (double)length;
}

int media_seek(Media *m, double seconds)
{
    if (m == NULL || !m->has_sound) return -1;
    if (seconds < 0.0) seconds = 0.0;

    if (ma_sound_seek_to_second(&m->sound, (float)seconds) != MA_SUCCESS)
        return -2;

    return 0;
}

int media_is_playing(const Media *m)
{
    if (m == NULL || !m->has_sound) return 0;
    return ma_sound_is_playing(&m->sound) ? 1 : 0;
}

int media_is_paused(const Media *m)
{
    if (m == NULL || !m->has_sound) return 0;
    return ma_sound_is_playing(&m->sound) ? 0 : 1;
}

void media_clear(Media *m)
{
    if (m == NULL) return;

    media_unload(m);

    if (!m->ready) return;
    ma_engine_uninit(&m->engine);
    m->ready = 0;
}
