# ESP32-S3 INMP441 麦克风端

本工程是从 `robotdogsay` 复制出的独立开发副本，原工程不参与修改。
当前固件只负责按键控制的麦克风采集和 WebSocket 上传，不负责 PC 端语音识别，也不负责机器狗动作控制。

## 硬件接线

INMP441：

```text
VDD  -> 3.3V
GND  -> GND
SCK  -> ESP32 GPIO14
WS   -> ESP32 GPIO15
SD   -> ESP32 GPIO16
L/R  -> GND
```

`SCK` 是 I2S 串行时钟，不能接地。`L/R` 接地表示选择左声道。

按键：

```text
ESP32 GPIO9 -> 按键一端
GND        -> 按键另一端
```

GPIO9 使用内部上拉：

```text
GPIO9 低电平：开始采集
GPIO9 高电平：停止采集
```

板载状态灯默认按单颗 WS2812/RGB 灯处理，数据脚配置为 GPIO48。真正开始 I2S 采集时点亮白灯，松开按键或采集启动失败时熄灭。若使用 ESP32-S3-DevKitC v1.1，请将 `main/app_config.h` 中的 `APP_STATUS_LED_GPIO` 改为 `GPIO_NUM_38`。

## 行为

- WiFi 启动后连接配置的 WebSocket URL。
- WebSocket 连接成功后发送小智风格 `hello` JSON，并等待服务端返回 `hello` 和 `session_id`。
- 不按按钮时不发送音频。
- 按住按钮时发送 `listen/start`，读取 INMP441，转为 16 位 PCM 后编码成 Opus 上传。
- 松开按钮后发送 `listen/stop`，立即停止发送，不缓存录音。
- WiFi 或 WebSocket 断开时丢弃音频，自动重连后恢复。
- 按键使用约 20 ms 消抖。

## 音频协议

WebSocket URL 配置在：

```text
main/wifi_config_private.h
```

默认结构：

```text
ws://<PC_IP>:8765?role=xiaozhi
```

设备连接时附带 WebSocket 请求头：

```text
Protocol-Version: 1
Device-Id: <WiFi MAC>
Client-Id: <WiFi MAC 去掉冒号>
Authorization: Bearer <VOICE_PC_WS_TOKEN>    # token 非空时才发送
```

连接成功后设备发送文本帧：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

服务端必须返回文本帧：

```json
{
  "type": "hello",
  "transport": "websocket",
  "session_id": "your-session-id"
}
```

按下按钮后，设备发送：

```json
{
  "session_id": "your-session-id",
  "type": "listen",
  "state": "start",
  "mode": "manual"
}
```

随后设备发送 WebSocket 二进制消息，内容是 Opus 音频包：

```text
编码格式：Opus
采样率：16000 Hz
声道：单声道
帧时长：60 ms
协议版本：3

Version 3 的 binary payload 格式：

```text
字节 0：type = 0
字节 1：reserved = 0
字节 2~3：Opus payload 长度，大端序
字节 4~：Opus frame
```
```

松开按钮后，设备发送：

```json
{
  "session_id": "your-session-id",
  "type": "listen",
  "state": "stop"
}
```

PC 端应按 WebSocket 消息边界逐帧处理二进制 Opus 包，不能再把二进制内容当 PCM。服务端流程应为：

```text
收到 listen/start
  -> 开始一次语音输入
收到 binary Opus frame
  -> Opus 解码为 16 kHz mono signed int16 PCM
  -> 送入 ASR
收到 listen/stop
  -> 结束本次语音输入
```

## 编译

ESP-IDF 版本：6.0.2。

```bash
cd /home/wh/esp/esp32s3robotsay
export IDF_TOOLS_PATH=/home/wh/esp/.espressif
. /home/wh/esp/esp-idf/export.sh
idf.py build
```

编译前修改：

```text
main/wifi_config_private.h
```

设置实际 WiFi 和 PC WebSocket 地址。
