#include "audio_player.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "audio";

typedef enum {
    AUDIO_REQ_PLAY_CLIP = 0,
    AUDIO_REQ_STOP = 1,
} audio_request_type_t;

typedef struct {
    audio_request_type_t type;
    fixed_clip_id_t clip_id;
} audio_request_t;

typedef struct {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_size;
    uint32_t data_offset;
} wav_info_t;

static i2s_chan_handle_t s_tx_handle;
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static int16_t *s_input_buf;
static int16_t *s_output_buf;
static volatile uint8_t s_volume_level = APP_DEFAULT_VOLUME_LEVEL;
static volatile bool s_busy;
static volatile bool s_stop_requested;
static bool s_i2s_enabled;

static uint16_t read_u16_le(FILE *fp)
{
    uint8_t b[2];
    return fread(b, 1, sizeof(b), fp) == sizeof(b) ? (uint16_t)(b[0] | ((uint16_t)b[1] << 8)) : 0xffff;
}

static uint32_t read_u32_le(FILE *fp)
{
    uint8_t b[4];
    return fread(b, 1, sizeof(b), fp) == sizeof(b)
        ? (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24)
        : 0xffffffffu;
}

static bool wav_read_header(FILE *fp, wav_info_t *info)
{
    uint8_t riff[12];
    if (fread(riff, 1, sizeof(riff), fp) != sizeof(riff)) {
        return false;
    }
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool got_fmt = false;
    bool got_data = false;

    while (!got_data) {
        uint8_t chunk_id[4];
        if (fread(chunk_id, 1, sizeof(chunk_id), fp) != sizeof(chunk_id)) {
            return false;
        }

        uint32_t chunk_size = read_u32_le(fp);
        if (chunk_size == 0xffffffffu) {
            return false;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            info->audio_format = read_u16_le(fp);
            info->channels = read_u16_le(fp);
            info->sample_rate = read_u32_le(fp);
            (void)read_u32_le(fp); // byte rate
            (void)read_u16_le(fp); // block align
            info->bits_per_sample = read_u16_le(fp);

            if (chunk_size > 16) {
                (void)fseek(fp, (long)(chunk_size - 16), SEEK_CUR);
            }
            got_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            info->data_size = chunk_size;
            info->data_offset = (uint32_t)ftell(fp);
            got_data = true;
            break;
        } else {
            (void)fseek(fp, (long)chunk_size, SEEK_CUR);
        }

        if ((chunk_size & 1u) != 0u) {
            (void)fseek(fp, 1, SEEK_CUR);
        }
    }

    return got_fmt && got_data;
}

static int16_t apply_volume(int16_t sample)
{
    uint8_t vol = s_volume_level;
    if (vol == 0) {
        return 0;
    }

    int32_t scaled = ((int32_t)sample * vol) / APP_MAX_VOLUME_LEVEL;
    if (scaled > INT16_MAX) {
        scaled = INT16_MAX;
    } else if (scaled < INT16_MIN) {
        scaled = INT16_MIN;
    }
    return (int16_t)scaled;
}

static esp_err_t ensure_i2s_enabled(void)
{
    if (s_i2s_enabled) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "i2s_channel_enable failed");
    s_i2s_enabled = true;
    return ESP_OK;
}

static esp_err_t ensure_i2s_disabled(void)
{
    if (!s_i2s_enabled) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_handle), TAG, "i2s_channel_disable failed");
    s_i2s_enabled = false;
    return ESP_OK;
}

static esp_err_t write_stereo_block(const int16_t *mono_samples, size_t mono_sample_count)
{
    for (size_t i = 0; i < mono_sample_count; ++i) {
        int16_t s = apply_volume(mono_samples[i]);
        s_output_buf[i * 2] = s;
        s_output_buf[i * 2 + 1] = s;
    }

    size_t bytes_written = 0;
    size_t bytes_to_write = mono_sample_count * 2 * sizeof(int16_t);
    ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx_handle, s_output_buf, bytes_to_write, &bytes_written, 1000),
                        TAG,
                        "i2s_channel_write failed");
    if (bytes_written != bytes_to_write) {
        ESP_LOGW(TAG, "i2s write short: want=%u got=%u",
                 (unsigned)bytes_to_write,
                 (unsigned)bytes_written);
    }
    return ESP_OK;
}

static esp_err_t write_silence_frames(size_t frame_count)
{
    size_t max_frames = APP_AUDIO_SOURCE_CHUNK_BYTES / sizeof(int16_t);
    while (frame_count > 0) {
        size_t now = frame_count > max_frames ? max_frames : frame_count;
        memset(s_output_buf, 0, now * 2 * sizeof(int16_t));

        size_t bytes_written = 0;
        size_t bytes_to_write = now * 2 * sizeof(int16_t);
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx_handle, s_output_buf, bytes_to_write, &bytes_written, 1000),
                            TAG,
                            "silence write failed");
        frame_count -= now;
    }
    return ESP_OK;
}

static esp_err_t play_wav_file(const fixed_clip_t *clip)
{
    FILE *fp = fopen(clip->path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "无法打开音频文件: %s", clip->path);
        return ESP_FAIL;
    }

    wav_info_t info = {0};
    if (!wav_read_header(fp, &info)) {
        fclose(fp);
        ESP_LOGE(TAG, "WAV 头解析失败: %s", clip->path);
        return ESP_FAIL;
    }

    if (info.audio_format != 1 || info.channels != 1 || info.bits_per_sample != 16 || info.sample_rate != APP_AUDIO_SAMPLE_RATE_HZ) {
        fclose(fp);
        ESP_LOGE(TAG,
                 "WAV 格式不匹配: fmt=%u ch=%u bits=%u rate=%u",
                 info.audio_format,
                 info.channels,
                 info.bits_per_sample,
                 info.sample_rate);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "播放: %s (%s)", clip->text, clip->path);

    ESP_RETURN_ON_ERROR(ensure_i2s_enabled(), TAG, "enable I2S failed");

    // 先补一小段静音，避免 MAX98357A 在起播瞬间“啪”一下。
    ESP_RETURN_ON_ERROR(write_silence_frames(128), TAG, "pre-roll silence failed");

    size_t data_left = info.data_size;
    while (data_left > 0) {
        if (s_stop_requested) {
            break;
        }

        size_t bytes_to_read = data_left > APP_AUDIO_SOURCE_CHUNK_BYTES ? APP_AUDIO_SOURCE_CHUNK_BYTES : data_left;
        size_t bytes_read = fread(s_input_buf, 1, bytes_to_read, fp);
        if (bytes_read == 0) {
            break;
        }

        size_t mono_samples = bytes_read / sizeof(int16_t);
        if (mono_samples > 0) {
            ESP_RETURN_ON_ERROR(write_stereo_block(s_input_buf, mono_samples), TAG, "audio write failed");
        }

        data_left -= bytes_read;
        if ((bytes_read & 1u) != 0u) {
            (void)fgetc(fp);
            if (data_left > 0) {
                data_left--;
            }
        }
    }

    // 收尾补静音，确保播放结束后立即安静。
    ESP_RETURN_ON_ERROR(write_silence_frames(APP_AUDIO_STEREO_SILENCE_FRAMES), TAG, "tail silence failed");
    ESP_RETURN_ON_ERROR(ensure_i2s_disabled(), TAG, "disable I2S failed");

    fclose(fp);
    return ESP_OK;
}

static void audio_task(void *arg)
{
    (void)arg;

    audio_request_t req = {0};
    while (true) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (req.type == AUDIO_REQ_STOP) {
            s_stop_requested = true;
            xQueueReset(s_queue);
            ESP_LOGI(TAG, "收到停止指令");
            continue;
        }

        const fixed_clip_t *clip = fixed_clip_get(req.clip_id);
        if (clip == NULL) {
            ESP_LOGW(TAG, "未知音频编号");
            continue;
        }

        s_busy = true;
        s_stop_requested = false;
        esp_err_t err = play_wav_file(clip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "播放失败: %s", esp_err_to_name(err));
        }
        s_busy = false;
    }
}

esp_err_t audio_player_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_input_buf = heap_caps_malloc(APP_AUDIO_SOURCE_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_output_buf = heap_caps_malloc(APP_AUDIO_SOURCE_CHUNK_BYTES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_input_buf == NULL || s_output_buf == NULL) {
        ESP_LOGE(TAG, "PSRAM 缓冲分配失败");
        return ESP_ERR_NO_MEM;
    }

    s_queue = xQueueCreate(APP_AUDIO_QUEUE_DEPTH, sizeof(audio_request_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "音频请求队列创建失败");
        return ESP_ERR_NO_MEM;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(APP_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(APP_AUDIO_BITS_PER_SAMPLE, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            // 固定引脚：BCLK/LRC/DIN 不能改，且必须避开 S3 的 Flash/PSRAM/USB/启动脚。
            .mclk = GPIO_NUM_NC,
            .bclk = APP_I2S_BCLK_GPIO,
            .ws = APP_I2S_WS_GPIO,
            .dout = APP_I2S_DOUT_GPIO,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "i2s_channel_init_std_mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "i2s_channel_enable failed");
    s_i2s_enabled = true;
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_handle), TAG, "i2s_channel_disable failed");
    s_i2s_enabled = false;

    BaseType_t ok = xTaskCreate(audio_task, "audio_task", APP_AUDIO_TASK_STACK_BYTES / sizeof(StackType_t), NULL, 5, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "音频任务创建失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "I2S 初始化成功: 16kHz / 16bit / stereo, BCLK=GPIO14 LRC=GPIO15 DIN=GPIO16");
    ESP_LOGI(TAG, "PSRAM 音频缓冲: input=%u bytes output=%u bytes",
             (unsigned)APP_AUDIO_SOURCE_CHUNK_BYTES,
             (unsigned)(APP_AUDIO_SOURCE_CHUNK_BYTES * 2));
    return ESP_OK;
}

esp_err_t audio_player_play_clip(fixed_clip_id_t clip_id)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    audio_request_t req = {
        .type = AUDIO_REQ_PLAY_CLIP,
        .clip_id = clip_id,
    };
    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t audio_player_play_demo_sequence(void)
{
    ESP_RETURN_ON_ERROR(audio_player_play_clip(FIXED_CLIP_BOOT), TAG, "queue boot clip failed");
    ESP_RETURN_ON_ERROR(audio_player_play_clip(FIXED_CLIP_HELLO), TAG, "queue hello clip failed");
    ESP_RETURN_ON_ERROR(audio_player_play_clip(FIXED_CLIP_STATUS), TAG, "queue status clip failed");
    return ESP_OK;
}

esp_err_t audio_player_set_volume(uint8_t volume)
{
    if (volume < APP_MIN_VOLUME_LEVEL) {
        volume = APP_MIN_VOLUME_LEVEL;
    }
    if (volume > APP_MAX_VOLUME_LEVEL) {
        volume = APP_MAX_VOLUME_LEVEL;
    }
    s_volume_level = volume;
    ESP_LOGI(TAG, "音量设置为 %u/9", (unsigned)volume);
    return ESP_OK;
}

uint8_t audio_player_get_volume(void)
{
    return s_volume_level;
}

bool audio_player_is_busy(void)
{
    return s_busy;
}

esp_err_t audio_player_stop(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    audio_request_t req = {
        .type = AUDIO_REQ_STOP,
        .clip_id = FIXED_CLIP_BOOT,
    };
    s_stop_requested = true;
    xQueueReset(s_queue);
    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
