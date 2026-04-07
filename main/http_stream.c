/**
 * HTTP MJPEG stream + remote upload.
 */

#include "http_stream.h"
#include "config.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "http";
static httpd_handle_t s_server = NULL;

static uint8_t *s_stream_buf = NULL;
static size_t s_stream_cap = 0;
static size_t s_stream_len = 0;
static int64_t s_last_upload_us = 0;
static int64_t s_last_upload_log_us = 0;

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

esp_err_t http_stream_start(int port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 2;
    config.stack_size = 8192;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
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

void http_stream_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

static const char *UPLOAD_URL = "http://101.33.209.65:8081/upload";

void http_stream_upload(const uint8_t *jpeg_data, size_t len)
{
    int64_t now = esp_timer_get_time();
    if ((now - s_last_upload_us) < 200000) {
        return;
    }
    s_last_upload_us = now;

    esp_http_client_config_t cfg = {
        .url = UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 2000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return;
    }

    esp_http_client_set_post_field(client, (const char *)jpeg_data, len);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK && (now - s_last_upload_log_us) >= 5000000) {
        ESP_LOGW(TAG, "Upload failed to %s: %s", UPLOAD_URL, esp_err_to_name(err));
        s_last_upload_log_us = now;
    }

    esp_http_client_cleanup(client);
}
