#include "status_led.h"

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"

static const char *TAG = "status_led";
static led_strip_handle_t s_led_strip;
static SemaphoreHandle_t s_led_mutex;

esp_err_t status_led_init(void)
{
    if (s_led_strip != NULL) {
        return ESP_OK;
    }
    if (s_led_mutex == NULL) {
        s_led_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_led_mutex != NULL, ESP_ERR_NO_MEM, TAG, "LED mutex create failed");
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = APP_STATUS_LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config,
                                                 &rmt_config,
                                                 &s_led_strip),
                        TAG,
                        "LED strip init failed");
    ESP_RETURN_ON_ERROR(led_strip_clear(s_led_strip), TAG, "LED clear failed");

    ESP_LOGI(TAG, "Onboard status LED ready on GPIO%d", APP_STATUS_LED_GPIO);
    return ESP_OK;
}

esp_err_t status_led_set_capture(bool active)
{
    if (s_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if (active) {
        ret = led_strip_set_pixel(s_led_strip, 0, 16, 16, 16);
        if (ret == ESP_OK) {
            ret = led_strip_refresh(s_led_strip);
        }
    } else {
        ret = led_strip_clear(s_led_strip);
    }
    xSemaphoreGive(s_led_mutex);

    return ret;
}
