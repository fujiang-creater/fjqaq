#include "fixed_clips.h"

#include <string.h>

#include "app_config.h"

const fixed_clip_t g_fixed_clips[FIXED_CLIP_COUNT] = {
    [FIXED_CLIP_BOOT] = {
        .id = FIXED_CLIP_BOOT,
        .key = "1",
        .path = APP_AUDIO_BOOT_WAV_PATH,
        .text = "系统启动完成",
    },
    [FIXED_CLIP_HELLO] = {
        .id = FIXED_CLIP_HELLO,
        .key = "2",
        .path = APP_AUDIO_HELLO_WAV_PATH,
        .text = "你好，我已经准备好了",
    },
    [FIXED_CLIP_STATUS] = {
        .id = FIXED_CLIP_STATUS,
        .key = "3",
        .path = APP_AUDIO_STATUS_WAV_PATH,
        .text = "音频播放正常",
    },
};

const fixed_clip_t *fixed_clip_get(fixed_clip_id_t id)
{
    if (id < 0 || id >= FIXED_CLIP_COUNT) {
        return NULL;
    }
    return &g_fixed_clips[id];
}

const fixed_clip_t *fixed_clip_find_by_key(const char *key)
{
    if (key == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < FIXED_CLIP_COUNT; ++i) {
        if (strcmp(g_fixed_clips[i].key, key) == 0) {
            return &g_fixed_clips[i];
        }
    }
    return NULL;
}
