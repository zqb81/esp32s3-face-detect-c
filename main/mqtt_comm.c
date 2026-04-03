/**
 * MQTT 通信实现
 */

#include "mqtt_comm.h"
#include "config.h"
#include "esp_log.h"
#include <stdbool.h>
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <string.h>
#include "mbedtls/base64.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

// ===== MQTT 事件回调 =====
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT 已连接");
            s_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT 断开");
            s_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT 错误");
            s_connected = false;
            break;
        default:
            break;
    }
}

esp_err_t mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = MQTT_BROKER,
        .broker.address.port = MQTT_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id = CLIENT_ID,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "MQTT 初始化失败");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "MQTT 连接中: %s:%d", MQTT_BROKER, MQTT_PORT);
    return ESP_OK;
}

esp_err_t mqtt_send_faces(const face_list_t *faces)
{
    if (!s_connected || !faces || faces->count == 0) {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", CLIENT_ID);

    // 时间戳（简单实现）
    int64_t ts = esp_timer_get_time() / 1000000;
    cJSON_AddNumberToObject(root, "ts", ts);
    cJSON_AddNumberToObject(root, "count", faces->count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < faces->count; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddNumberToObject(f, "score", faces->faces[i].score);
        cJSON *box = cJSON_CreateIntArray((int[]){
            faces->faces[i].x1, faces->faces[i].y1,
            faces->faces[i].x2, faces->faces[i].y2
        }, 4);
        cJSON_AddItemToObject(f, "box", box);
        cJSON_AddItemToArray(arr, f);
    }
    cJSON_AddItemToObject(root, "faces", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        esp_mqtt_client_publish(s_client, MQTT_TOPIC, json_str, 0, 1, 0);
        free(json_str);
    }

    return ESP_OK;
}

esp_err_t mqtt_send_face_crop(const uint8_t *rgb565, int img_w, int img_h,
                              const face_result_t *face)
{
    if (!s_connected || !face) {
        return ESP_OK;
    }

    // 裁剪人脸区域
    int x1 = face->x1; if (x1 < 0) x1 = 0;
    int y1 = face->y1; if (y1 < 0) y1 = 0;
    int x2 = face->x2; if (x2 >= img_w) x2 = img_w - 1;
    int y2 = face->y2; if (y2 >= img_h) y2 = img_h - 1;
    int sw = x2 - x1, sh = y2 - y1;
    if (sw <= 0 || sh <= 0) return ESP_OK;

    // 采样到 CROP_SIZE x CROP_SIZE
    uint8_t *crop = heap_caps_malloc(FACE_CROP_SIZE * FACE_CROP_SIZE * 2, MALLOC_CAP_SPIRAM);
    if (!crop) return ESP_ERR_NO_MEM;

    for (int dy = 0; dy < FACE_CROP_SIZE; dy++) {
        int sy = y1 + dy * sh / FACE_CROP_SIZE;
        if (sy >= img_h) sy = img_h - 1;
        for (int dx = 0; dx < FACE_CROP_SIZE; dx++) {
            int sx = x1 + dx * sw / FACE_CROP_SIZE;
            if (sx >= img_w) sx = img_w - 1;
            int src_off = (sy * img_w + sx) * 2;
            int dst_off = (dy * FACE_CROP_SIZE + dx) * 2;
            crop[dst_off] = rgb565[src_off];
            crop[dst_off + 1] = rgb565[src_off + 1];
        }
    }

    // Base64 编码
    size_t raw_len = FACE_CROP_SIZE * FACE_CROP_SIZE * 2;
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, crop, raw_len);
    char *b64 = malloc(b64_len + 1);
    if (b64) {
        mbedtls_base64_encode((unsigned char *)b64, b64_len, &b64_len, crop, raw_len);
        b64[b64_len] = '\0';

        // JSON
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "face_crop");
        cJSON_AddStringToObject(root, "device", CLIENT_ID);
        cJSON_AddNumberToObject(root, "score", face->score);
        cJSON_AddNumberToObject(root, "img_w", FACE_CROP_SIZE);
        cJSON_AddNumberToObject(root, "img_h", FACE_CROP_SIZE);
        cJSON_AddStringToObject(root, "img_data", b64);

        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (json) {
            esp_mqtt_client_publish(s_client, MQTT_TOPIC_CROP, json, 0, 1, 0);
            free(json);
        }
        free(b64);
    }

    free(crop);
    return ESP_OK;
}

void mqtt_deinit(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
}
