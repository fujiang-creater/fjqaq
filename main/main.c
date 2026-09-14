#include "mic_board.h"
#include "mic_ws.h"
#include "wifi_sta.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "app_main";

static esp_err_t app_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void app_main(void)
{
    ESP_ERROR_CHECK(app_nvs_init());
    ESP_LOGI(TAG, "Starting ESP32-S3 INMP441 microphone client");
    ESP_ERROR_CHECK(wifi_sta_init());
    ESP_ERROR_CHECK(mic_ws_start());
    ESP_ERROR_CHECK(mic_board_init());
    ESP_LOGI(TAG, "Ready: hold GPIO9 button to stream Opus audio");
}
