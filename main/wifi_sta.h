#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "wifi_config_private.h"

esp_err_t wifi_sta_init(void);
bool wifi_sta_is_connected(void);
