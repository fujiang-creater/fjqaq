#include "opus_encoder.h"

#include "esp_audio_enc.h"
#include "esp_audio_types.h"
#include "esp_log.h"
#include "esp_opus_enc.h"

#include "app_config.h"

static const char *TAG = "opus_encoder";

static void *s_encoder;
static int s_frame_bytes;
static int s_outbuf_size;

static esp_opus_enc_frame_duration_t opus_frame_duration(void)
{
    switch (APP_OPUS_FRAME_DURATION_MS) {
    case 5:
        return ESP_OPUS_ENC_FRAME_DURATION_5_MS;
    case 10:
        return ESP_OPUS_ENC_FRAME_DURATION_10_MS;
    case 20:
        return ESP_OPUS_ENC_FRAME_DURATION_20_MS;
    case 40:
        return ESP_OPUS_ENC_FRAME_DURATION_40_MS;
    case 60:
        return ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    case 80:
        return ESP_OPUS_ENC_FRAME_DURATION_80_MS;
    case 100:
        return ESP_OPUS_ENC_FRAME_DURATION_100_MS;
    case 120:
        return ESP_OPUS_ENC_FRAME_DURATION_120_MS;
    default:
        return ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    }
}

esp_err_t mic_opus_encoder_init(void)
{
    if (s_encoder != NULL) {
        return ESP_OK;
    }

    esp_opus_enc_config_t config = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = opus_frame_duration(),
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };

    esp_audio_err_t ret = esp_opus_enc_open(&config, sizeof(config), &s_encoder);
    if (ret != ESP_AUDIO_ERR_OK || s_encoder == NULL) {
        ESP_LOGE(TAG, "esp_opus_enc_open failed: %d", ret);
        return ESP_FAIL;
    }

    ret = esp_opus_enc_get_frame_size(s_encoder, &s_frame_bytes, &s_outbuf_size);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "esp_opus_enc_get_frame_size failed: %d", ret);
        esp_opus_enc_close(s_encoder);
        s_encoder = NULL;
        return ESP_FAIL;
    }

    if ((size_t)s_frame_bytes != APP_AUDIO_FRAME_BYTES) {
        ESP_LOGW(TAG,
                 "Opus input frame is %d bytes, app frame is %u bytes",
                 s_frame_bytes,
                 (unsigned)APP_AUDIO_FRAME_BYTES);
    }

    ESP_LOGI(TAG,
             "Opus encoder ready: %d input bytes, %d output bytes, %d ms",
             s_frame_bytes,
             s_outbuf_size,
             APP_OPUS_FRAME_DURATION_MS);
    return ESP_OK;
}

esp_err_t mic_opus_encoder_encode(const void *pcm,
                                  size_t pcm_bytes,
                                  uint8_t *out,
                                  size_t out_capacity,
                                  size_t *out_len)
{
    if (pcm == NULL || out == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_encoder == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm_bytes != (size_t)s_frame_bytes || out_capacity < (size_t)s_outbuf_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_audio_enc_in_frame_t in_frame = {
        .buffer = (uint8_t *)pcm,
        .len = (uint32_t)pcm_bytes,
    };
    esp_audio_enc_out_frame_t out_frame = {
        .buffer = out,
        .len = (uint32_t)out_capacity,
        .encoded_bytes = 0,
    };

    esp_audio_err_t ret = esp_opus_enc_process(s_encoder, &in_frame, &out_frame);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "esp_opus_enc_process failed: %d", ret);
        return ESP_FAIL;
    }

    *out_len = out_frame.encoded_bytes;
    return ESP_OK;
}

size_t mic_opus_encoder_input_bytes(void)
{
    return s_frame_bytes > 0 ? (size_t)s_frame_bytes : 0;
}

size_t mic_opus_encoder_output_capacity(void)
{
    return s_outbuf_size > 0 ? (size_t)s_outbuf_size : 0;
}
