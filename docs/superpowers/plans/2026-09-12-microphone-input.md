# ESP32-S3 Microphone Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the copied `robotdogsay` project into an ESP32-S3 INMP441 push-to-talk microphone client that sends raw 16 kHz, 16-bit, mono PCM to a configurable PC WebSocket endpoint.

**Architecture:** The new project keeps the original project untouched and uses a small, focused firmware: WiFi station setup, a reconnecting WebSocket client, an I2S standard-mode RX channel, and a GPIO9 active-low push-to-talk gate. I2S samples are read as 32-bit stereo slots, the selected left slot is converted to signed 16-bit little-endian PCM, and exactly 320 samples are sent per 20 ms frame while the button is held.

**Tech Stack:** ESP-IDF 6.0.2, ESP32-S3, `esp_driver_i2s`, `esp_websocket_client` managed component, FreeRTOS, C11, host-side `cc` test for PCM conversion.

## Global Constraints

- Develop only in `/home/wh/esp/esp32s3robotsay`; `/home/wh/esp/robotdogsay` remains unchanged.
- Use INMP441 `SCK/BCLK=GPIO14`, `WS=GPIO15`, `SD=GPIO16`, `L/R=GND`.
- Use active-low push-to-talk button on `GPIO9` with the internal pull-up; pressed means GPIO9 low.
- Send only binary WebSocket payloads containing raw little-endian signed 16-bit mono PCM.
- Audio format is 16000 Hz, 16-bit, mono; normal frame size is 320 samples / 640 bytes.
- Do not implement PC ASR, control JSON, MAX98357 playback, or control-end WebSocket handling in this phase.
- WiFi credentials and the full `ws://...?...role=mic` URL live in `main/wifi_config_private.h`, with an example file beside it.

### Task 1: Add a testable PCM conversion module

**Files:**
- Create: `/home/wh/esp/esp32s3robotsay/main/pcm_convert.h`
- Create: `/home/wh/esp/esp32s3robotsay/main/pcm_convert.c`
- Create: `/home/wh/esp/esp32s3robotsay/tests/test_pcm_convert.c`

**Interfaces:**
- Produces `size_t pcm_convert_left_i2s32_to_s16le(const int32_t *src, size_t src_count, uint8_t *dst, size_t dst_capacity)`.
- The input is interleaved left/right 32-bit I2S slot data; `src[0]` is the left slot and `src[1]` is the right slot.
- The output contains only left-slot samples, encoded as little-endian signed 16-bit values.

- [ ] **Step 1: Write the failing host test**

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../main/pcm_convert.h"

int main(void)
{
    const int32_t input[] = {
        0x00123400, 0x00777700,
        -0x00200000, 0x00011100,
        0x007fffff, 0x00888800,
    };
    uint8_t output[6] = {0};

    size_t written = pcm_convert_left_i2s32_to_s16le(input, 3, output, sizeof(output));

    assert(written == 6);
    assert(output[0] == 0x34 && output[1] == 0x12);
    assert(output[2] == 0xE0 && output[3] == 0xFF);
    assert(output[4] == 0xFF && output[5] == 0x7F);
    puts("pcm conversion test passed");
    return 0;
}
```

- [ ] **Step 2: Run the test and verify the expected missing-symbol failure**

Run:

```bash
cd /home/wh/esp/esp32s3robotsay
cc -std=c11 -Wall -Wextra -Werror tests/test_pcm_convert.c main/pcm_convert.c -I main -o /tmp/test_pcm_convert
```

Expected before implementation: compilation fails because `main/pcm_convert.c` and the conversion symbol do not exist.

- [ ] **Step 3: Implement the minimal conversion**

For each left slot, shift the signed 32-bit I2S value right by 8 bits, clamp to `INT16_MIN..INT16_MAX`, and write the low byte followed by the high byte. Return zero for invalid pointers, odd slot counts, or insufficient output capacity.

- [ ] **Step 4: Run the test and verify it passes**

Run:

```bash
cc -std=c11 -Wall -Wextra -Werror tests/test_pcm_convert.c main/pcm_convert.c -I main -o /tmp/test_pcm_convert
/tmp/test_pcm_convert
```

Expected output:

```text
pcm conversion test passed
```

### Task 2: Replace playback configuration with microphone configuration

**Files:**
- Modify: `/home/wh/esp/esp32s3robotsay/main/app_config.h`
- Create: `/home/wh/esp/esp32s3robotsay/main/wifi_config_private.example.h`
- Create: `/home/wh/esp/esp32s3robotsay/main/wifi_config_private.h`
- Modify: `/home/wh/esp/esp32s3robotsay/sdkconfig.defaults`

**Interfaces:**
- Exposes `APP_I2S_SCK_GPIO`, `APP_I2S_WS_GPIO`, `APP_I2S_SD_GPIO`, `APP_PTT_GPIO`.
- Exposes `APP_AUDIO_SAMPLE_RATE_HZ=16000`, `APP_AUDIO_FRAME_SAMPLES=320`, `APP_AUDIO_FRAME_BYTES=640`.
- `wifi_config_private.h` defines `WIFI_STA_SSID`, `WIFI_STA_PASSWORD`, and `VOICE_PC_WS_URL`.

- [ ] **Step 1: Add the private configuration files**

Use this shape, with local WiFi credentials kept only in the private config and a replaceable placeholder URL:

```c
#pragma once

#define WIFI_STA_SSID "your-wifi-ssid"
#define WIFI_STA_PASSWORD "your-wifi-password"
#define VOICE_PC_WS_URL "ws://192.168.1.100:8765?role=mic"
```

The example file uses `"your-wifi-ssid"`, `"your-wifi-password"`, and `"ws://192.168.1.100:8765?role=mic"`.

- [ ] **Step 2: Replace playback constants**

Set the microphone pins and frame constants in `app_config.h`; remove playback paths, queue constants, SPIFFS paths, volume constants, and MAX98357 pin definitions from the active configuration.

- [ ] **Step 3: Update defaults**

Keep ESP32-S3, 16 MB Flash, Octal PSRAM, and 80 MHz PSRAM settings. Remove the USB console and custom SPIFFS partition requirements because the microphone firmware has no filesystem payload.

### Task 3: Implement WiFi and reconnecting WebSocket transport

**Files:**
- Create: `/home/wh/esp/esp32s3robotsay/main/wifi_sta.h`
- Create: `/home/wh/esp/esp32s3robotsay/main/wifi_sta.c`
- Create: `/home/wh/esp/esp32s3robotsay/main/mic_ws.h`
- Create: `/home/wh/esp/esp32s3robotsay/main/mic_ws.c`
- Modify: `/home/wh/esp/esp32s3robotsay/main/idf_component.yml`

**Interfaces:**
- `esp_err_t wifi_sta_init(void)`.
- `bool wifi_sta_is_connected(void)`.
- `esp_err_t mic_ws_start(void)`.
- `bool mic_ws_is_connected(void)`.
- `esp_err_t mic_ws_send_pcm(const void *data, size_t len)`.

- [ ] **Step 1: Add the WebSocket dependency**

Declare the managed component `espressif/esp_websocket_client` and retain the IDF dependency. The component must provide `esp_websocket_client.h`.

- [ ] **Step 2: Implement WiFi station state**

Initialize `esp_netif`, the default event loop, STA mode, and credentials from `wifi_config_private.h`. Set a connected bit on `IP_EVENT_STA_GOT_IP`, clear it on `WIFI_EVENT_STA_DISCONNECTED`, and reconnect automatically after disconnect.

- [ ] **Step 3: Implement the WebSocket client**

Create one client with `.uri = VOICE_PC_WS_URL`, automatic reconnect enabled, and an event callback that tracks `WEBSOCKET_EVENT_CONNECTED` and `WEBSOCKET_EVENT_DISCONNECTED`. Start it after WiFi initialization; sending is allowed only while the client is connected.

- [ ] **Step 4: Make send behavior bounded**

`mic_ws_send_pcm` must return `ESP_ERR_INVALID_STATE` when disconnected, reject zero-length or oversized payloads, and call `esp_websocket_client_send_bin` without allocating a second copy.

### Task 4: Implement INMP441 I2S RX and push-to-talk capture

**Files:**
- Create: `/home/wh/esp/esp32s3robotsay/main/mic_board.h`
- Create: `/home/wh/esp/esp32s3robotsay/main/mic_board.c`

**Interfaces:**
- `esp_err_t mic_board_init(void)`.
- A FreeRTOS capture task reads I2S only while GPIO9 is stably low.

- [ ] **Step 1: Configure GPIO9**

Configure `APP_PTT_GPIO` as input with `GPIO_PULLUP_ENABLE`, `GPIO_PULLDOWN_DISABLE`, and `GPIO_INTR_DISABLE`. Treat low as pressed.

- [ ] **Step 2: Configure I2S standard RX**

Create an RX channel with `I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER)`. Use:

```c
.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                             I2S_SLOT_MODE_STEREO),
.gpio_cfg = {
    .mclk = I2S_GPIO_UNUSED,
    .bclk = GPIO_NUM_14,
    .ws = GPIO_NUM_15,
    .dout = I2S_GPIO_UNUSED,
    .din = GPIO_NUM_16,
}
```

Select the left slot in the slot configuration and enable the channel once during initialization.

- [ ] **Step 3: Implement the capture loop**

Allocate an interleaved `int32_t` input buffer for 640 samples and a 640-byte PCM output buffer. Debounce the button by requiring the same GPIO state for at least 20 ms. While pressed, read complete I2S blocks, convert the selected left channel with `pcm_convert_left_i2s32_to_s16le`, and call `mic_ws_send_pcm`. While released or disconnected, flush/drop input data rather than caching it.

- [ ] **Step 4: Handle transitions**

On a press transition, flush stale I2S input before the first frame. On a release transition, stop sending immediately and discard any partially built frame. Log connection and capture state changes without logging raw audio payloads.

### Task 5: Integrate the microphone firmware and remove playback from the build

**Files:**
- Modify: `/home/wh/esp/esp32s3robotsay/main/main.c`
- Modify: `/home/wh/esp/esp32s3robotsay/main/CMakeLists.txt`
- Modify: `/home/wh/esp/esp32s3robotsay/CMakeLists.txt`
- Modify: `/home/wh/esp/esp32s3robotsay/README.md`

- [ ] **Step 1: Register only microphone sources**

Compile `main.c`, `wifi_sta.c`, `mic_ws.c`, `mic_board.c`, and `pcm_convert.c`; require WiFi, netif, NVS, event loop, `esp_driver_i2s`, GPIO, FreeRTOS, and WebSocket components. Do not compile the old playback, console, fixed-clip, or SPIFFS source files.

- [ ] **Step 2: Initialize in a stable order**

In `app_main`, initialize NVS, WiFi, WebSocket, and microphone hardware. Keep the task alive after initialization. Do not initialize SPIFFS, audio playback, or USB console REPL.

- [ ] **Step 3: Document the device contract**

Update the README with the corrected INMP441 wiring, button wiring, URL configuration, raw PCM format, 640-byte normal frame size, reconnect behavior, and the fact that PC silence of roughly 300 ms indicates button release.

### Task 6: Verify the copy and build

**Files:**
- Generated: `/home/wh/esp/esp32s3robotsay/build/**`

- [ ] **Step 1: Run the host conversion test**

Run `/tmp/test_pcm_convert` and require exit code 0.

- [ ] **Step 2: Configure and build with ESP-IDF 6.0.2**

Run:

```bash
cd /home/wh/esp/esp32s3robotsay
export IDF_TOOLS_PATH=/home/wh/esp/.espressif
. /home/wh/esp/esp-idf/export.sh
idf.py reconfigure
idf.py build
```

Expected: the project configures for `esp32s3`, compiles without errors, and emits the application binary under `build/`.

- [ ] **Step 3: Confirm the source project is unchanged**

Run:

```bash
rsync -nrc --delete \
  --exclude '/build/' \
  --exclude '/docs/' \
  /home/wh/esp/robotdogsay/ \
  /home/wh/esp/esp32s3robotsay/
```

Expected: no differences are reported for files shared with the original project except intentionally changed project files in the new copy.
