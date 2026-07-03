/**
 * HTTP MJPEG stream + async remote upload.
 */

#include "http_stream.h"
#include "config.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http";
// HTTP_UPLOAD_URL 定义在 config_private.h
static const char *UPLOAD_URL = HTTP_UPLOAD_URL;

static httpd_handle_t s_server = NULL;

static uint8_t *s_stream_buf = NULL;
static size_t s_stream_cap = 0;
static size_t s_stream_len = 0;

static uint8_t *s_upload_pending_buf = NULL;
static uint8_t *s_upload_work_buf = NULL;
static size_t s_upload_pending_len = 0;
static bool s_upload_pending = false;
static TaskHandle_t s_upload_task = NULL;
static SemaphoreHandle_t s_upload_mutex = NULL;

typedef struct {
    uint32_t attempts;
    uint32_t success;
    uint32_t failed;
    uint64_t total_us;
    uint32_t max_us;
    esp_err_t last_err;
    int64_t last_log_us;
} upload_stats_t;

static upload_stats_t s_upload_stats = {
    .last_err = ESP_OK,
};
static int64_t s_last_upload_us = 0;
static int64_t s_last_submit_drop_log_us = 0;

static const char index_html[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>ESP32-S3</title>"
    "<style>body{margin:0;background:#111;display:flex;flex-direction:column;"
    "align-items:center;justify-content:center;height:100vh;font-family:sans-serif;color:#eee}"
    "h2{margin:10px 0 5px;font-size:14px;color:#888}"
    "img{border-radius:8px;box-shadow:0 0 20px rgba(0,0,0,.5)}"
    "#info{margin-top:10px;font-size:12px;color:#666}</style></head>"
    "<body><h2>ESP32-S3 Live</h2>"
    "<img id='v' src='/stream'>"
    "<div id='info'>MJPEG Stream</div></body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res;
    char hdr[128];

    res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    if (res != ESP_OK) {
        return res;
    }

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    while (1) {
        if (!s_stream_buf || s_stream_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int hdr_len = snprintf(
            hdr, sizeof(hdr), "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", s_stream_len);
        res = httpd_resp_send_chunk(req, hdr, hdr_len);
        if (res != ESP_OK) {
            break;
        }

        res = httpd_resp_send_chunk(req, (const char *)s_stream_buf, s_stream_len);
        if (res != ESP_OK) {
            break;
        }

        res = httpd_resp_send_chunk(req, "\r\n", 2);
        if (res != ESP_OK) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(33));
    }

    return ESP_OK;
}

static void upload_stats_record(int64_t start_us, esp_err_t err)
{
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    if (elapsed_us < 0) {
        elapsed_us = 0;
    }

    s_upload_stats.attempts++;
    s_upload_stats.total_us += (uint64_t)elapsed_us;
    if ((uint32_t)elapsed_us > s_upload_stats.max_us) {
        s_upload_stats.max_us = (uint32_t)elapsed_us;
    }

    if (err == ESP_OK) {
        s_upload_stats.success++;
    } else {
        s_upload_stats.failed++;
        s_upload_stats.last_err = err;
    }

    int64_t now = esp_timer_get_time();
    if ((now - s_upload_stats.last_log_us) >= ((int64_t)PERF_LOG_INTERVAL_MS * 1000) &&
        s_upload_stats.attempts > 0) {
        float avg_ms = (float)s_upload_stats.total_us / 1000.0f / (float)s_upload_stats.attempts;
        float max_ms = (float)s_upload_stats.max_us / 1000.0f;
        ESP_LOGW(TAG,
                 "UPLOAD avg:%.1fms max:%.1fms ok:%" PRIu32 " fail:%" PRIu32 " last:%s",
                 avg_ms,
                 max_ms,
                 s_upload_stats.success,
                 s_upload_stats.failed,
                 esp_err_to_name(s_upload_stats.last_err));

        s_upload_stats.attempts = 0;
        s_upload_stats.success = 0;
        s_upload_stats.failed = 0;
        s_upload_stats.total_us = 0;
        s_upload_stats.max_us = 0;
        s_upload_stats.last_log_us = now;
    }
}

static bool upload_take_pending(size_t *len)
{
    bool have_frame = false;

    if (!s_upload_mutex || !s_upload_pending_buf || !s_upload_work_buf) {
        return false;
    }

    if (xSemaphoreTake(s_upload_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_upload_pending && s_upload_pending_len > 0) {
            memcpy(s_upload_work_buf, s_upload_pending_buf, s_upload_pending_len);
            if (len) {
                *len = s_upload_pending_len;
            }
            s_upload_pending = false;
            s_upload_pending_len = 0;
            have_frame = true;
        }
        xSemaphoreGive(s_upload_mutex);
    }

    return have_frame;
}

static void upload_task(void *arg)
{
    while (1) {
        size_t len = 0;
        if (!upload_take_pending(&len)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        while (len > 0) {

            int64_t now = esp_timer_get_time();
            int64_t remaining_us = ((int64_t)HTTP_UPLOAD_MIN_INTERVAL_MS * 1000) - (now - s_last_upload_us);
            if (remaining_us > 0) {
                vTaskDelay(pdMS_TO_TICKS((remaining_us + 999) / 1000));
            }

            int64_t start_us = esp_timer_get_time();
            esp_http_client_config_t cfg = {
                .url = UPLOAD_URL,
                .method = HTTP_METHOD_POST,
                .timeout_ms = HTTP_UPLOAD_TIMEOUT_MS,
            };

            esp_http_client_handle_t client = esp_http_client_init(&cfg);
            esp_err_t err = ESP_FAIL;
            if (client) {
                esp_http_client_set_post_field(client, (const char *)s_upload_work_buf, len);
                esp_http_client_set_header(client, "Content-Type", "image/jpeg");
                err = esp_http_client_perform(client);
                esp_http_client_cleanup(client);
            }

            s_last_upload_us = esp_timer_get_time();
            upload_stats_record(start_us, err);

            len = 0;
            upload_take_pending(&len);
        }
    }
}

static esp_err_t upload_worker_start(void)
{
    s_upload_pending_buf =
        (uint8_t *)heap_caps_malloc(HTTP_UPLOAD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_upload_work_buf =
        (uint8_t *)heap_caps_malloc(HTTP_UPLOAD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_upload_mutex = xSemaphoreCreateMutex();

    if (!s_upload_pending_buf || !s_upload_work_buf || !s_upload_mutex) {
        ESP_LOGE(TAG, "Upload worker alloc failed");
        free(s_upload_pending_buf);
        free(s_upload_work_buf);
        s_upload_pending_buf = NULL;
        s_upload_work_buf = NULL;
        if (s_upload_mutex) {
            vSemaphoreDelete(s_upload_mutex);
            s_upload_mutex = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(upload_task,
                                            "upload",
                                            HTTP_UPLOAD_TASK_STACK_SIZE,
                                            NULL,
                                            HTTP_UPLOAD_TASK_PRIORITY,
                                            &s_upload_task,
                                            tskNO_AFFINITY);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Upload task create failed");
        free(s_upload_pending_buf);
        free(s_upload_work_buf);
        s_upload_pending_buf = NULL;
        s_upload_work_buf = NULL;
        vSemaphoreDelete(s_upload_mutex);
        s_upload_mutex = NULL;
        s_upload_task = NULL;
        return ESP_FAIL;
    }

    s_upload_stats.last_log_us = esp_timer_get_time();
    return ESP_OK;
}

esp_err_t http_stream_start(int port)
{
    esp_err_t err = upload_worker_start();
    if (err != ESP_OK) {
        return err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 2;
    config.stack_size = 8192;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        if (s_upload_task) {
            vTaskDelete(s_upload_task);
            s_upload_task = NULL;
        }
        if (s_upload_mutex) {
            vSemaphoreDelete(s_upload_mutex);
            s_upload_mutex = NULL;
        }
        free(s_upload_pending_buf);
        free(s_upload_work_buf);
        s_upload_pending_buf = NULL;
        s_upload_work_buf = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &index_uri);

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &stream_uri);

    ESP_LOGI(TAG, "HTTP stream: http://0.0.0.0:%d", port);
    ESP_LOGI(TAG, "HTTP upload worker ready: %s", UPLOAD_URL);
    return ESP_OK;
}

void http_stream_update_frame(const uint8_t *jpeg_data, size_t len)
{
    if (!jpeg_data || len == 0) {
        return;
    }

    if (len > s_stream_cap) {
        free(s_stream_buf);
        s_stream_buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_stream_buf) {
            s_stream_cap = 0;
            s_stream_len = 0;
            ESP_LOGW(TAG, "Stream buffer alloc failed: %zu", len);
            return;
        }
        s_stream_cap = len;
    }

    memcpy(s_stream_buf, jpeg_data, len);
    s_stream_len = len;
}

void http_stream_submit_upload(const uint8_t *jpeg_data, size_t len)
{
    int64_t now = esp_timer_get_time();

    if (!jpeg_data || len == 0 || !s_upload_task || !s_upload_mutex || !s_upload_pending_buf) {
        return;
    }

    if (len > HTTP_UPLOAD_BUFFER_SIZE) {
        if ((now - s_last_submit_drop_log_us) >= 5000000) {
            ESP_LOGW(TAG, "Upload frame too large: %zu > %d", len, HTTP_UPLOAD_BUFFER_SIZE);
            s_last_submit_drop_log_us = now;
        }
        return;
    }

    if (xSemaphoreTake(s_upload_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(s_upload_pending_buf, jpeg_data, len);
        s_upload_pending_len = len;
        s_upload_pending = true;
        xSemaphoreGive(s_upload_mutex);
        xTaskNotifyGive(s_upload_task);
    }
}

void http_stream_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    if (s_upload_task) {
        vTaskDelete(s_upload_task);
        s_upload_task = NULL;
    }
    if (s_upload_mutex) {
        vSemaphoreDelete(s_upload_mutex);
        s_upload_mutex = NULL;
    }

    free(s_upload_pending_buf);
    free(s_upload_work_buf);
    free(s_stream_buf);
    s_upload_pending_buf = NULL;
    s_upload_work_buf = NULL;
    s_stream_buf = NULL;
    s_stream_cap = 0;
    s_stream_len = 0;
    s_upload_pending_len = 0;
    s_upload_pending = false;
}
