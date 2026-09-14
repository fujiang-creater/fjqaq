#include "console_cmd.h"

#include <stdio.h>
#include <string.h>

#include "audio_player.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "console";
static esp_console_repl_t *s_repl;

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n可用命令:\n");
    printf("  h   - 打印帮助\n");
    printf("  1   - 播放系统启动完成\n");
    printf("  2   - 播放你好，我已经准备好了\n");
    printf("  3   - 播放音频播放正常\n");
    printf("  t   - 1 的别名\n");
    printf("  s   - 2 的别名\n");
    printf("  c   - 按顺序播放三句测试语音\n");
    printf("  v0-v9 - 设置音量\n");
    printf("当前音量: %u/9\n\n", (unsigned)audio_player_get_volume());
    return 0;
}

static int cmd_play_clip0(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = audio_player_play_clip(FIXED_CLIP_BOOT);
    printf("%s\n", err == ESP_OK ? "已排队播放: 系统启动完成" : "播放排队失败");
    return err == ESP_OK ? 0 : 1;
}

static int cmd_play_clip1(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = audio_player_play_clip(FIXED_CLIP_HELLO);
    printf("%s\n", err == ESP_OK ? "已排队播放: 你好，我已经准备好了" : "播放排队失败");
    return err == ESP_OK ? 0 : 1;
}

static int cmd_play_clip2(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = audio_player_play_clip(FIXED_CLIP_STATUS);
    printf("%s\n", err == ESP_OK ? "已排队播放: 音频播放正常" : "播放排队失败");
    return err == ESP_OK ? 0 : 1;
}

static int cmd_cycle(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = audio_player_play_demo_sequence();
    printf("%s\n", err == ESP_OK ? "已排队连续播放三句测试语音" : "播放排队失败");
    return err == ESP_OK ? 0 : 1;
}

#define DEFINE_VOLUME_CMD(N) \
    static int cmd_volume_##N(int argc, char **argv) { \
        (void)argc; \
        (void)argv; \
        esp_err_t err = audio_player_set_volume(N); \
        printf("音量已设置为 %u/9\n", (unsigned)N); \
        return err == ESP_OK ? 0 : 1; \
    }

DEFINE_VOLUME_CMD(0)
DEFINE_VOLUME_CMD(1)
DEFINE_VOLUME_CMD(2)
DEFINE_VOLUME_CMD(3)
DEFINE_VOLUME_CMD(4)
DEFINE_VOLUME_CMD(5)
DEFINE_VOLUME_CMD(6)
DEFINE_VOLUME_CMD(7)
DEFINE_VOLUME_CMD(8)
DEFINE_VOLUME_CMD(9)

static esp_err_t register_cmd(const char *name, const char *help, esp_console_cmd_func_t func)
{
    const esp_console_cmd_t cmd = {
        .command = name,
        .help = help,
        .hint = NULL,
        .func = func,
    };
    return esp_console_cmd_register(&cmd);
}

static void register_all_commands(void)
{
    ESP_ERROR_CHECK(register_cmd("h", "打印帮助", cmd_help));
    ESP_ERROR_CHECK(register_cmd("1", "播放系统启动完成", cmd_play_clip0));
    ESP_ERROR_CHECK(register_cmd("2", "播放你好，我已经准备好了", cmd_play_clip1));
    ESP_ERROR_CHECK(register_cmd("3", "播放音频播放正常", cmd_play_clip2));
    ESP_ERROR_CHECK(register_cmd("t", "1 的别名", cmd_play_clip0));
    ESP_ERROR_CHECK(register_cmd("s", "2 的别名", cmd_play_clip1));
    ESP_ERROR_CHECK(register_cmd("c", "顺序播放三句测试语音", cmd_cycle));

    ESP_ERROR_CHECK(register_cmd("v0", "音量 0", cmd_volume_0));
    ESP_ERROR_CHECK(register_cmd("v1", "音量 1", cmd_volume_1));
    ESP_ERROR_CHECK(register_cmd("v2", "音量 2", cmd_volume_2));
    ESP_ERROR_CHECK(register_cmd("v3", "音量 3", cmd_volume_3));
    ESP_ERROR_CHECK(register_cmd("v4", "音量 4", cmd_volume_4));
    ESP_ERROR_CHECK(register_cmd("v5", "音量 5", cmd_volume_5));
    ESP_ERROR_CHECK(register_cmd("v6", "音量 6", cmd_volume_6));
    ESP_ERROR_CHECK(register_cmd("v7", "音量 7", cmd_volume_7));
    ESP_ERROR_CHECK(register_cmd("v8", "音量 8", cmd_volume_8));
    ESP_ERROR_CHECK(register_cmd("v9", "音量 9", cmd_volume_9));
}

static void repl_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_console_start_repl(s_repl));
    vTaskDelete(NULL);
}

esp_err_t console_cmd_start(void)
{
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "robotdogsay> ";
    repl_config.max_cmdline_length = 64;

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dev_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config, &s_repl), TAG, "USB Serial/JTAG REPL init failed");
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t dev_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_cdc(&dev_config, &repl_config, &s_repl), TAG, "USB CDC REPL init failed");
#else
    esp_console_dev_uart_config_t dev_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(&dev_config, &repl_config, &s_repl), TAG, "UART REPL init failed");
#endif

    register_all_commands();

    BaseType_t ok = xTaskCreate(repl_task, "console_repl", APP_CONSOLE_TASK_STACK_BYTES / sizeof(StackType_t), NULL, 5, NULL);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "串口命令已启动: h / 1 / 2 / 3 / t / s / c / v0-v9");
    return ESP_OK;
}
