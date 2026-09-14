#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t mic_opus_encoder_init(void);
esp_err_t mic_opus_encoder_encode(const void *pcm,
                                  size_t pcm_bytes,
                                  uint8_t *out,
                                  size_t out_capacity,
                                  size_t *out_len);
size_t mic_opus_encoder_input_bytes(void);
size_t mic_opus_encoder_output_capacity(void);
