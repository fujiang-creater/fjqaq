#include "spiffs_mount.h"

#include <stdio.h>

#include "app_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "spiffs";
static bool s_mounted;
static size_t s_total_bytes;
static size_t s_used_bytes;

bool spiffs_mount_or_abort(void)
{
    if (s_mounted) {
        return true;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = APP_SPIFFS_BASE_PATH,
        .partition_label = APP_SPIFFS_LABEL,
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS 挂载失败: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_spiffs_info(APP_SPIFFS_LABEL, &s_total_bytes, &s_used_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS 信息读取失败: %s", esp_err_to_name(err));
        esp_vfs_spiffs_unregister(APP_SPIFFS_LABEL);
        return false;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SPIFFS 挂载成功: total=%u KB used=%u KB path=%s",
             (unsigned)(s_total_bytes / 1024),
             (unsigned)(s_used_bytes / 1024),
             APP_SPIFFS_BASE_PATH);
    return true;
}

const char *spiffs_root_path(void)
{
    return APP_SPIFFS_BASE_PATH;
}

void spiffs_print_usage(void)
{
    if (!s_mounted) {
        ESP_LOGW(TAG, "SPIFFS 尚未挂载");
        return;
    }

    ESP_LOGI(TAG, "SPIFFS 使用情况: total=%u KB used=%u KB free=%u KB",
             (unsigned)(s_total_bytes / 1024),
             (unsigned)(s_used_bytes / 1024),
             (unsigned)((s_total_bytes - s_used_bytes) / 1024));
}
