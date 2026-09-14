#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t mic_ws_start(void);
bool mic_ws_is_connected(void);
bool mic_ws_is_ready(void);
esp_err_t mic_ws_start_listening(void);
esp_err_t mic_ws_stop_listening(void);
esp_err_t mic_ws_send_pcm(const void *data, size_t len);
