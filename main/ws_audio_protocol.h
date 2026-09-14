#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Wrap one Opus frame using the Xiaozhi WebSocket binary protocol version 3:
 * type=0, reserved=0, payload_size in network byte order, then Opus data.
 */
size_t mic_ws_wrap_opus_v3(const uint8_t *opus,
                           size_t opus_len,
                           uint8_t *out,
                           size_t out_capacity);
