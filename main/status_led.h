#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t status_led_init(void);
esp_err_t status_led_set_capture(bool active);
