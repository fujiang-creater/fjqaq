#include "ws_audio_protocol.h"

#include <limits.h>
#include <string.h>

#define MIC_WS_V3_HEADER_BYTES 4u

size_t mic_ws_wrap_opus_v3(const uint8_t *opus,
                           size_t opus_len,
                           uint8_t *out,
                           size_t out_capacity)
{
    if (out == NULL || opus_len > UINT16_MAX ||
        (opus_len > 0 && opus == NULL) ||
        out_capacity < MIC_WS_V3_HEADER_BYTES + opus_len) {
        return 0;
    }

    out[0] = 0;
    out[1] = 0;
    out[2] = (uint8_t)(opus_len >> 8);
    out[3] = (uint8_t)(opus_len & 0xffu);
    if (opus_len > 0) {
        memcpy(out + MIC_WS_V3_HEADER_BYTES, opus, opus_len);
    }

    return MIC_WS_V3_HEADER_BYTES + opus_len;
}
