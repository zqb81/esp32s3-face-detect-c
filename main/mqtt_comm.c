/**
 * MQTT 通信实现
 */

#include "mqtt_comm.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdbool.h>
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include "mbedtls/base64.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

#define FACE_CROP_RAW_SIZE (FACE_CROP_SIZE * FACE_CROP_SIZE * 2)

typedef struct {
    int64_t ts;
    float score;
    int box[4];
    int img_w;
    int img_h;
} face_crop_job_t;

static uint8_t *s_face_crop_pending_buf = NULL;
static uint8_t *s_face_crop_work_buf = NULL;
static bool s_face_crop_pending = false;
static face_crop_job_t s_face_crop_pending_job = {0};
static TaskHandle_t s_face_crop_task = NULL;
static SemaphoreHandle_t s_face_crop_mutex = NULL;
static int64_t s_last_face_crop_submit_us = 0;

static esp_err_t face_crop_worker_start(void);
static esp_err_t face_crop_extract(const uint8_t *rgb565,
                                   int img_w,
                                   int img_h,
                                   const face_result_t *face,
                                   uint8_t *crop_buf,
                                   face_crop_job_t *job);
static bool face_crop_take_pending(face_crop_job_t *job);
static esp_err_t face_crop_publish(const uint8_t *crop_buf, const face_crop_job_t *job);
static esp_err_t face_crop_send_sync(const uint8_t *rgb565, int img_w, int img_h, const face_result_t *face);
static void face_crop_task(void *arg);

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

    esp_err_t crop_worker_ret = face_crop_worker_start();
    if (crop_worker_ret != ESP_OK) {
        ESP_LOGW(TAG, "人脸抓拍异步任务启动失败，将回退到同步发送: %s", esp_err_to_name(crop_worker_ret));
    }

    ESP_LOGI(TAG, "MQTT 连接中: %s:%d", MQTT_BROKER, MQTT_PORT);
    return ESP_OK;
}

esp_err_t mqtt_send_faces(const face_list_t *faces, int frame, int img_w, int img_h)
{
    if (!s_connected || !faces || faces->count == 0) {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", CLIENT_ID);

    // 时间戳（简单实现）
    int64_t ts = esp_timer_get_time() / 1000000;
    cJSON_AddNumberToObject(root, "ts", ts);
    cJSON_AddNumberToObject(root, "frame", frame);
    cJSON_AddNumberToObject(root, "count", faces->count);
    cJSON_AddNumberToObject(root, "img_w", img_w);
    cJSON_AddNumberToObject(root, "img_h", img_h);

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

static esp_err_t face_crop_extract(const uint8_t *rgb565,
                                   int img_w,
                                   int img_h,
                                   const face_result_t *face,
                                   uint8_t *crop_buf,
                                   face_crop_job_t *job)
{
    if (!rgb565 || !face || !crop_buf || !job) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(job, 0, sizeof(*job));
    job->ts = esp_timer_get_time() / 1000000;
    job->score = face->score;

    int x1 = face->x1;
    if (x1 < 0) {
        x1 = 0;
    }
    int y1 = face->y1;
    if (y1 < 0) {
        y1 = 0;
    }
    int x2 = face->x2;
    if (x2 >= img_w) {
        x2 = img_w - 1;
    }
    int y2 = face->y2;
    if (y2 >= img_h) {
        y2 = img_h - 1;
    }

    int sw = x2 - x1;
    int sh = y2 - y1;
    if (sw <= 0 || sh <= 0) {
        return ESP_OK;
    }

    job->img_w = FACE_CROP_SIZE;
    job->img_h = FACE_CROP_SIZE;
    job->box[0] = x1;
    job->box[1] = y1;
    job->box[2] = x2;
    job->box[3] = y2;

    for (int dy = 0; dy < FACE_CROP_SIZE; dy++) {
        int sy = y1 + dy * sh / FACE_CROP_SIZE;
        if (sy >= img_h) {
            sy = img_h - 1;
        }
        for (int dx = 0; dx < FACE_CROP_SIZE; dx++) {
            int sx = x1 + dx * sw / FACE_CROP_SIZE;
            if (sx >= img_w) {
                sx = img_w - 1;
            }
            int src_off = (sy * img_w + sx) * 2;
            int dst_off = (dy * FACE_CROP_SIZE + dx) * 2;
            crop_buf[dst_off] = rgb565[src_off];
            crop_buf[dst_off + 1] = rgb565[src_off + 1];
        }
    }

    return ESP_OK;
}

static esp_err_t face_crop_publish(const uint8_t *crop_buf, const face_crop_job_t *job)
{
    if (!s_connected || !crop_buf || !job) {
        return ESP_OK;
    }

    size_t raw_len = FACE_CROP_RAW_SIZE;
    size_t b64_len = 0;
    int b64_ret = mbedtls_base64_encode(NULL, 0, &b64_len, crop_buf, raw_len);
    if (b64_ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || b64_len == 0) {
        ESP_LOGW(TAG, "计算抓拍图 Base64 长度失败: ret=%d len=%u", b64_ret, (unsigned)b64_len);
        return ESP_FAIL;
    }

    char *b64 = malloc(b64_len + 1);
    if (b64) {
        b64_ret = mbedtls_base64_encode((unsigned char *)b64, b64_len + 1, &b64_len, crop_buf, raw_len);
        if (b64_ret != 0) {
            ESP_LOGW(TAG, "抓拍图 Base64 编码失败: ret=%d", b64_ret);
            free(b64);
            return ESP_FAIL;
        }
        b64[b64_len] = '\0';

        cJSON *root = cJSON_CreateObject();
        if (!root) {
            free(b64);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(root, "type", "face_crop");
        cJSON_AddStringToObject(root, "device", CLIENT_ID);
        cJSON_AddNumberToObject(root, "ts", job->ts);
        cJSON_AddNumberToObject(root, "score", job->score);
        cJSON_AddNumberToObject(root, "img_w", job->img_w);
        cJSON_AddNumberToObject(root, "img_h", job->img_h);
        cJSON_AddStringToObject(root, "img_data", b64);
        cJSON_AddItemToObject(root, "box", cJSON_CreateIntArray(job->box, 4));

        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (!json) {
            free(b64);
            return ESP_ERR_NO_MEM;
        }

        int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_CROP, json, 0, 1, 0);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "发布人脸抓拍失败: topic=%s", MQTT_TOPIC_CROP);
            free(json);
            free(b64);
            return ESP_FAIL;
        }

        free(json);
        free(b64);
        return ESP_OK;
    }

    return ESP_ERR_NO_MEM;
}

static bool face_crop_take_pending(face_crop_job_t *job)
{
    bool have_job = false;

    if (!s_face_crop_mutex || !s_face_crop_pending_buf || !s_face_crop_work_buf) {
        return false;
    }

    if (xSemaphoreTake(s_face_crop_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_face_crop_pending) {
            memcpy(s_face_crop_work_buf, s_face_crop_pending_buf, FACE_CROP_RAW_SIZE);
            if (job) {
                *job = s_face_crop_pending_job;
            }
            s_face_crop_pending = false;
            have_job = true;
        }
        xSemaphoreGive(s_face_crop_mutex);
    }

    return have_job;
}

static void face_crop_task(void *arg)
{
    while (1) {
        face_crop_job_t job = {0};
        if (!face_crop_take_pending(&job)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        while (job.img_w > 0 && job.img_h > 0) {
            face_crop_publish(s_face_crop_work_buf, &job);
            memset(&job, 0, sizeof(job));
            face_crop_take_pending(&job);
        }
    }
}

static esp_err_t face_crop_worker_start(void)
{
    if (s_face_crop_task) {
        return ESP_OK;
    }

    s_face_crop_pending_buf = heap_caps_malloc(FACE_CROP_RAW_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_face_crop_work_buf = heap_caps_malloc(FACE_CROP_RAW_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_face_crop_mutex = xSemaphoreCreateMutex();

    if (!s_face_crop_pending_buf || !s_face_crop_work_buf || !s_face_crop_mutex) {
        free(s_face_crop_pending_buf);
        free(s_face_crop_work_buf);
        s_face_crop_pending_buf = NULL;
        s_face_crop_work_buf = NULL;
        if (s_face_crop_mutex) {
            vSemaphoreDelete(s_face_crop_mutex);
            s_face_crop_mutex = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(face_crop_task,
                                            "face_crop",
                                            FACE_CROP_TASK_STACK_SIZE,
                                            NULL,
                                            FACE_CROP_TASK_PRIORITY,
                                            &s_face_crop_task,
                                            tskNO_AFFINITY);
    if (ok != pdPASS) {
        free(s_face_crop_pending_buf);
        free(s_face_crop_work_buf);
        s_face_crop_pending_buf = NULL;
        s_face_crop_work_buf = NULL;
        vSemaphoreDelete(s_face_crop_mutex);
        s_face_crop_mutex = NULL;
        s_face_crop_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "人脸抓拍异步任务已启动");
    return ESP_OK;
}

static esp_err_t face_crop_send_sync(const uint8_t *rgb565, int img_w, int img_h, const face_result_t *face)
{
    uint8_t *crop = heap_caps_malloc(FACE_CROP_RAW_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!crop) {
        return ESP_ERR_NO_MEM;
    }

    face_crop_job_t job = {0};
    esp_err_t err = face_crop_extract(rgb565, img_w, img_h, face, crop, &job);
    if (err == ESP_OK && job.img_w > 0 && job.img_h > 0) {
        err = face_crop_publish(crop, &job);
    }

    free(crop);
    return err;
}

esp_err_t mqtt_send_face_crop(const uint8_t *rgb565, int img_w, int img_h,
                              const face_result_t *face)
{
    if (!rgb565 || !face) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected) {
        return ESP_OK;
    }

    int64_t now_us = esp_timer_get_time();
    if (FACE_CROP_INTERVAL_MS > 0 &&
        (now_us - s_last_face_crop_submit_us) < ((int64_t)FACE_CROP_INTERVAL_MS * 1000)) {
        return ESP_OK;
    }

    if (!s_face_crop_task || !s_face_crop_mutex || !s_face_crop_pending_buf) {
        esp_err_t err = face_crop_send_sync(rgb565, img_w, img_h, face);
        if (err == ESP_OK) {
            s_last_face_crop_submit_us = now_us;
        }
        return err;
    }

    if (xSemaphoreTake(s_face_crop_mutex, portMAX_DELAY) == pdTRUE) {
        face_crop_job_t job = {0};
        esp_err_t err = face_crop_extract(rgb565, img_w, img_h, face, s_face_crop_pending_buf, &job);
        if (err == ESP_OK && job.img_w > 0 && job.img_h > 0) {
            s_face_crop_pending_job = job;
            s_face_crop_pending = true;
            s_last_face_crop_submit_us = now_us;
            xSemaphoreGive(s_face_crop_mutex);
            xTaskNotifyGive(s_face_crop_task);
            return ESP_OK;
        }
        xSemaphoreGive(s_face_crop_mutex);
        return err;
    }

    return ESP_FAIL;
}

void mqtt_deinit(void)
{
    if (s_face_crop_task) {
        vTaskDelete(s_face_crop_task);
        s_face_crop_task = NULL;
    }
    if (s_face_crop_mutex) {
        vSemaphoreDelete(s_face_crop_mutex);
        s_face_crop_mutex = NULL;
    }
    free(s_face_crop_pending_buf);
    free(s_face_crop_work_buf);
    s_face_crop_pending_buf = NULL;
    s_face_crop_work_buf = NULL;
    s_face_crop_pending = false;

    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
}
