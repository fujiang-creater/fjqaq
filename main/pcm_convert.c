#include "pcm_convert.h"

#include <limits.h>

size_t pcm_convert_left_i2s32_to_s16le(const int32_t *src,
                                       size_t src_slot_count,
                                       uint8_t *dst,
                                       size_t dst_capacity)
{
    if (src == NULL || dst == NULL || src_slot_count == 0 || (src_slot_count & 1u) != 0u) {
        return 0;
    }

    size_t sample_count = src_slot_count / 2u;
    if (sample_count > dst_capacity / sizeof(int16_t)) {
        return 0;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        /* INMP441's 24-bit sample occupies the upper 24 bits of the 32-bit slot. */
        int32_t sample = src[i * 2u] >> 16;
        if (sample > INT16_MAX) {
            sample = INT16_MAX;
        } else if (sample < INT16_MIN) {
            sample = INT16_MIN;
        }

        uint16_t encoded = (uint16_t)(int16_t)sample;
        dst[i * 2u] = (uint8_t)(encoded & 0xffu);
        dst[i * 2u + 1u] = (uint8_t)(encoded >> 8);
    }

    return sample_count * sizeof(int16_t);
}
