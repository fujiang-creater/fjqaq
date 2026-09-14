#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "../main/pcm_convert.h"

int main(void)
{
    const int32_t input[] = {
        0x12340000, 0x77770000,
        -0x20000000, 0x01110000,
        0x7fffff00, 0x88880000,
    };
    uint8_t output[6] = {0};

    size_t written = pcm_convert_left_i2s32_to_s16le(input, sizeof(input) / sizeof(input[0]), output, sizeof(output));

    assert(written == 6);
    assert(output[0] == 0x34 && output[1] == 0x12);
    assert(output[2] == 0x00 && output[3] == 0xE0);
    assert(output[4] == 0xFF && output[5] == 0x7F);

    const int32_t extremes[] = {
        INT32_MAX, 0,
        INT32_MIN, 0,
    };
    uint8_t clamped[4] = {0};
    written = pcm_convert_left_i2s32_to_s16le(extremes, 4, clamped, sizeof(clamped));
    assert(written == 4);
    assert(clamped[0] == 0xFF && clamped[1] == 0x7F);
    assert(clamped[2] == 0x00 && clamped[3] == 0x80);

    assert(pcm_convert_left_i2s32_to_s16le(input, 3, output, sizeof(output)) == 0);
    assert(pcm_convert_left_i2s32_to_s16le(input, 6, output, 5) == 0);

    puts("pcm conversion test passed");
    return 0;
}
