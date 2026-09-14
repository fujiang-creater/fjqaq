#pragma once

#include <stddef.h>

typedef enum {
    FIXED_CLIP_BOOT = 0,
    FIXED_CLIP_HELLO,
    FIXED_CLIP_STATUS,
    FIXED_CLIP_COUNT,
} fixed_clip_id_t;

typedef struct {
    fixed_clip_id_t id;
    const char *key;
    const char *path;
    const char *text;
} fixed_clip_t;

extern const fixed_clip_t g_fixed_clips[FIXED_CLIP_COUNT];

const fixed_clip_t *fixed_clip_get(fixed_clip_id_t id);
const fixed_clip_t *fixed_clip_find_by_key(const char *key);
