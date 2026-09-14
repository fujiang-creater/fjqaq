#include "mic_board.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "capture_gate.h"
#include "mic_ws.h"
#include "pcm_convert.h"
#include "status_led.h"

static const char *TAG = "mic_board";
static i2s_chan_handle_t s_rx_handle;
static TaskHandle_t s_capture_task;

#define CAPTURE_STACK_LOG_INTERVAL_FRAMES 50u

static bool ptt_is_pressed(void)
{
    return gpio_get_level(APP_PTT_GPIO) == 0;
}

static void discard_i2s_input(int32_t *input, size_t input_bytes)
{
    size_t bytes_read = 0;
    while (i2s_channel_read(s_rx_handle, input, input_bytes, &bytes_read, 0) == ESP_OK &&
           bytes_read > 0) {
    }
}

static bool read_debounced_ptt(bool previous)
{
    bool current = ptt_is_pressed();
    if (current == previous) {
        return previous;
    }

    vTaskDelay(pdMS_TO_TICKS(APP_PTT_DEBOUNCE_MS));
    return ptt_is_pressed();
}

static void stop_capture(bool *i2s_enabled, bool send_listen_stop, const char *reason)
{
    if (*i2s_enabled) {
        esp_err_t ret = i2s_channel_disable(s_rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S RX disable failed: %s", esp_err_to_name(ret));
        }
        *i2s_enabled = false;
    }

    if (send_listen_stop) {
        esp_err_t stop_ret = mic_ws_stop_listening();
        if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Stop listening failed: %s", esp_err_to_name(stop_ret));
        }
    }

    esp_err_t led_ret = status_led_set_capture(false);
    if (led_ret != ESP_OK) {
        ESP_LOGW(TAG, "Capture LED off failed: %s", esp_err_to_name(led_ret));
    }

    ESP_LOGI(TAG, "%s", reason);
}

static void capture_task(void *arg)
{
    (void)arg;

    static int32_t i2s_input[APP_I2S_SLOT_COUNT];
    static uint8_t pcm_output[APP_AUDIO_FRAME_BYTES];
    bool i2s_enabled = false;
    uint32_t sent_frames = 0;
    mic_capture_gate_t gate;

    mic_capture_gate_init(&gate);

    while (true) {
        bool stable_pressed = read_debounced_ptt(gate.button_pressed);
        mic_capture_gate_event_t gate_event = mic_capture_gate_update(&gate, stable_pressed);
        if (gate_event == MIC_CAPTURE_GATE_PRESS) {
            if (!mic_ws_is_ready()) {
                ESP_LOGW(TAG, "PTT pressed before WebSocket hello; waiting for release");
                mic_capture_gate_reject_press(&gate);
            } else {
                esp_err_t listen_ret = mic_ws_start_listening();
                if (listen_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Start listening failed: %s", esp_err_to_name(listen_ret));
                    mic_capture_gate_reject_press(&gate);
                } else {
                    esp_err_t ret = i2s_channel_enable(s_rx_handle);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "I2S RX enable failed: %s", esp_err_to_name(ret));
                        mic_ws_stop_listening();
                        status_led_set_capture(false);
                        mic_capture_gate_reject_press(&gate);
                    } else {
                        i2s_enabled = true;
                        sent_frames = 0;
                        discard_i2s_input(i2s_input, sizeof(i2s_input));
                        esp_err_t led_ret = status_led_set_capture(true);
                        if (led_ret != ESP_OK) {
                            ESP_LOGW(TAG, "Capture LED on failed: %s", esp_err_to_name(led_ret));
                        }
                        ESP_LOGI(TAG,
                                 "PTT pressed, capture started, stack free=%u bytes",
                                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
                    }
                }
            }
        } else if (gate_event == MIC_CAPTURE_GATE_RELEASE && i2s_enabled) {
            stop_capture(&i2s_enabled, true, "PTT released, capture stopped");
        }

        if (!i2s_enabled) {
            vTaskDelay(1);
            continue;
        }

        if (!mic_ws_is_ready()) {
            stop_capture(&i2s_enabled, false, "WebSocket lost, capture stopped");
            mic_capture_gate_transport_lost(&gate);
            continue;
        }

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_handle,
                                         i2s_input,
                                         sizeof(i2s_input),
                                         &bytes_read,
                                         100);
        if (ret != ESP_OK || bytes_read != sizeof(i2s_input)) {
            vTaskDelay(1);
            continue;
        }

        if (!ptt_is_pressed()) {
            mic_capture_gate_force_release(&gate);
            stop_capture(&i2s_enabled, true, "PTT released, capture stopped");
            continue;
        }

        size_t pcm_bytes = pcm_convert_left_i2s32_to_s16le(i2s_input,
                                                            bytes_read / sizeof(int32_t),
                                                            pcm_output,
                                                            sizeof(pcm_output));
        if (pcm_bytes != APP_AUDIO_FRAME_BYTES) {
            ESP_LOGW(TAG, "PCM conversion returned %u bytes", (unsigned)pcm_bytes);
            continue;
        }

        ret = mic_ws_send_pcm(pcm_output, pcm_bytes);
        if (ret == ESP_ERR_INVALID_STATE) {
            stop_capture(&i2s_enabled, false, "WebSocket not ready, capture stopped");
            mic_capture_gate_transport_lost(&gate);
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "PCM send failed: %s", esp_err_to_name(ret));
            continue;
        }

        ++sent_frames;
        if ((sent_frames % CAPTURE_STACK_LOG_INTERVAL_FRAMES) == 0u) {
            ESP_LOGI(TAG,
                     "Audio frames sent=%u, stack free=%u bytes",
                     (unsigned)sent_frames,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

esp_err_t mic_board_init(void)
{
    gpio_config_t ptt_config = {
        .pin_bit_mask = 1ULL << APP_PTT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&ptt_config), TAG, "PTT GPIO config failed");
    ESP_RETURN_ON_ERROR(status_led_init(), TAG, "Status LED init failed");

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_config.dma_desc_num = 6;
    chan_config.dma_frame_num = APP_I2S_DMA_FRAME_SAMPLES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_config, NULL, &s_rx_handle),
                        TAG,
                        "I2S RX channel create failed");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(APP_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = APP_I2S_SCK_GPIO,
            .ws = APP_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = APP_I2S_SD_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_handle, &std_config),
                        TAG,
                        "I2S RX mode init failed");

    BaseType_t task_ok = xTaskCreate(capture_task,
                                     "mic_capture",
                                     APP_CAPTURE_TASK_STACK_BYTES,
                                     NULL,
                                     6,
                                     &s_capture_task);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "INMP441 ready: 16kHz / 32-bit stereo RX, %d ms Opus frames, SCK=GPIO%d WS=GPIO%d SD=GPIO%d PTT=GPIO%d",
             APP_OPUS_FRAME_DURATION_MS,
             APP_I2S_SCK_GPIO,
             APP_I2S_WS_GPIO,
             APP_I2S_SD_GPIO,
             APP_PTT_GPIO);
    return ESP_OK;
}
