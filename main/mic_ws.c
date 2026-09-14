#include "mic_ws.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"

#include "app_config.h"
#include "opus_encoder.h"
#include "status_led.h"
#include "ws_audio_protocol.h"
#include "wifi_config_private.h"

#ifndef VOICE_PC_WS_TOKEN
#define VOICE_PC_WS_TOKEN ""
#endif

#ifndef VOICE_WS_PROTOCOL_VERSION
#define VOICE_WS_PROTOCOL_VERSION 3
#endif

static const char *TAG = "mic_ws";
static esp_websocket_client_handle_t s_client;
static volatile bool s_connected;
static volatile bool s_server_hello;
static volatile bool s_listening;
static char s_session_id[80];

static char s_headers[256];
static uint8_t s_binary_output[APP_WS_V3_HEADER_BYTES + APP_OPUS_OUTPUT_MAX_BYTES];

static esp_err_t mic_ws_send_text(const char *text)
{
    if (s_client == NULL || !s_connected || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int len = (int)strlen(text);
    int sent = esp_websocket_client_send_text(s_client, text, len, pdMS_TO_TICKS(1000));
    return sent == len ? ESP_OK : ESP_FAIL;
}

static void make_client_ids(char *device_id, size_t device_len, char *client_id, size_t client_len)
{
    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(device_id,
             device_len,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
    snprintf(client_id,
             client_len,
             "%02x%02x%02x%02x%02x%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}

static void build_headers(void)
{
    char device_id[32] = {0};
    char client_id[32] = {0};
    make_client_ids(device_id, sizeof(device_id), client_id, sizeof(client_id));

    if (strlen(VOICE_PC_WS_TOKEN) > 0) {
        snprintf(s_headers,
                 sizeof(s_headers),
                 "Authorization: Bearer %s\r\n"
                 "Protocol-Version: %d\r\n"
                 "Device-Id: %s\r\n"
                 "Client-Id: %s\r\n",
                 VOICE_PC_WS_TOKEN,
                 VOICE_WS_PROTOCOL_VERSION,
                 device_id,
                 client_id);
    } else {
        snprintf(s_headers,
                 sizeof(s_headers),
                 "Protocol-Version: %d\r\n"
                 "Device-Id: %s\r\n"
                 "Client-Id: %s\r\n",
                 VOICE_WS_PROTOCOL_VERSION,
                 device_id,
                 client_id);
    }
}

static esp_err_t send_hello(void)
{
    char hello[240];
    int len = snprintf(hello,
                       sizeof(hello),
                       "{\"type\":\"hello\",\"version\":%d,\"transport\":\"websocket\","
                       "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":%d,"
                       "\"channels\":1,\"frame_duration\":%d}}",
                       VOICE_WS_PROTOCOL_VERSION,
                       APP_AUDIO_SAMPLE_RATE_HZ,
                       APP_OPUS_FRAME_DURATION_MS);
    if (len < 0 || len >= (int)sizeof(hello)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return mic_ws_send_text(hello);
}

static void handle_text_message(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "Invalid JSON from server");
        return;
    }

    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "hello") == 0) {
        const cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
        if (cJSON_IsString(session_id)) {
            strlcpy(s_session_id, session_id->valuestring, sizeof(s_session_id));
        } else {
            s_session_id[0] = '\0';
        }
        s_server_hello = true;
        ESP_LOGI(TAG, "Server hello received, session_id=%s", s_session_id);
    } else if (cJSON_IsString(type)) {
        ESP_LOGI(TAG, "Server JSON type=%s ignored", type->valuestring);
    }

    cJSON_Delete(root);
}

static void websocket_event_handler(void *handler_args,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
    (void)handler_args;
    (void)event_base;
    (void)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        s_server_hello = false;
        s_listening = false;
        ESP_LOGI(TAG, "Connected to %s", VOICE_PC_WS_URL);
        if (send_hello() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send hello");
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        s_server_hello = false;
        s_listening = false;
        status_led_set_capture(false);
        ESP_LOGW(TAG, "Disconnected from PC WebSocket");
        break;
    case WEBSOCKET_EVENT_DATA: {
        esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
        if (data != NULL && data->op_code == WS_TRANSPORT_OPCODES_TEXT &&
            data->payload_offset == 0 && data->fin) {
            handle_text_message(data->data_ptr, data->data_len);
        }
        break;
    }
    case WEBSOCKET_EVENT_ERROR:
        s_connected = false;
        s_server_hello = false;
        s_listening = false;
        status_led_set_capture(false);
        ESP_LOGW(TAG, "WebSocket error");
        break;
    default:
        break;
    }
}

esp_err_t mic_ws_start(void)
{
    if (s_client != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mic_opus_encoder_init(), TAG, "Opus encoder init failed");
    build_headers();

    esp_websocket_client_config_t config = {
        .uri = VOICE_PC_WS_URL,
        .headers = s_headers,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 5000,
    };

    s_client = esp_websocket_client_init(&config);
    if (s_client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_websocket_register_events(s_client,
                                                      WEBSOCKET_EVENT_ANY,
                                                      websocket_event_handler,
                                                      NULL),
                        TAG,
                        "register WebSocket events failed");
    ESP_RETURN_ON_ERROR(esp_websocket_client_start(s_client), TAG, "WebSocket start failed");
    return ESP_OK;
}

bool mic_ws_is_connected(void)
{
    return s_client != NULL && s_connected;
}

bool mic_ws_is_ready(void)
{
    return mic_ws_is_connected() && s_server_hello;
}

esp_err_t mic_ws_start_listening(void)
{
    if (!mic_ws_is_ready() || s_session_id[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_listening) {
        return ESP_OK;
    }

    char message[160];
    int len = snprintf(message,
                       sizeof(message),
                       "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"start\","
                       "\"mode\":\"manual\"}",
                       s_session_id);
    if (len < 0 || len >= (int)sizeof(message)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = mic_ws_send_text(message);
    if (ret == ESP_OK) {
        s_listening = true;
    }
    return ret;
}

esp_err_t mic_ws_stop_listening(void)
{
    if (!mic_ws_is_ready() || s_session_id[0] == '\0') {
        s_listening = false;
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_listening) {
        return ESP_OK;
    }

    char message[128];
    int len = snprintf(message,
                       sizeof(message),
                       "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"}",
                       s_session_id);
    if (len < 0 || len >= (int)sizeof(message)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = mic_ws_send_text(message);
    s_listening = false;
    return ret;
}

esp_err_t mic_ws_send_pcm(const void *data, size_t len)
{
    if (data == NULL || len != APP_AUDIO_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mic_ws_is_ready() || !s_listening) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t opus_len = 0;
    esp_err_t ret = mic_opus_encoder_encode(data,
                                            len,
                                            s_binary_output + APP_WS_V3_HEADER_BYTES,
                                            APP_OPUS_OUTPUT_MAX_BYTES,
                                            &opus_len);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t packet_len = mic_ws_wrap_opus_v3(s_binary_output + APP_WS_V3_HEADER_BYTES,
                                            opus_len,
                                            s_binary_output,
                                            sizeof(s_binary_output));
    if (packet_len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    int sent = esp_websocket_client_send_bin(s_client,
                                             (const char *)s_binary_output,
                                             (int)packet_len,
                                             pdMS_TO_TICKS(1000));
    if (sent != (int)packet_len) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
