#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Convert interleaved 32-bit left/right I2S slots to signed 16-bit
 * little-endian PCM from the left slot.
 *
 * src_slot_count must be even. The returned value is the number of bytes
 * written to dst, or zero for invalid arguments.
 */
size_t pcm_convert_left_i2s32_to_s16le(const int32_t *src,
                                       size_t src_slot_count,
                                       uint8_t *dst,
                                       size_t dst_capacity);
