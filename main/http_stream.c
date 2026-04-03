/**
 * HTTP MJPEG 视频流 + 云端上传
 */

#include "http_stream.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include <string.h>

static const char *TAG = "http";
static httpd_handle_t s_server = NULL;
static const uint8_t *s_latest_jpeg = NULL;
static size_t s_jpeg_len = 0;

// ===== 播放页面 =====
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
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    while (1) {
        if (!s_latest_jpeg || s_jpeg_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int hdr_len = snprintf(hdr, sizeof(hdr),
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
            s_jpeg_len);
        res = httpd_resp_send_chunk(req, hdr, hdr_len);
        if (res != ESP_OK) break;

        res = httpd_resp_send_chunk(req, (const char *)s_latest_jpeg, s_jpeg_len);
        if (res != ESP_OK) break;

        res = httpd_resp_send_chunk(req, "\r\n", 2);
        if (res != ESP_OK) break;

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
        ESP_LOGE(TAG, "HTTP 服务器启动失败");
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

    ESP_LOGI(TAG, "HTTP 流: http://0.0.0.0:%d", port);
    return ESP_OK;
}

void http_stream_update_frame(const uint8_t *jpeg_data, size_t len)
{
    s_latest_jpeg = jpeg_data;
    s_jpeg_len = len;
}

void http_stream_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

// ===== 上传到云端 =====
static const char *UPLOAD_URL = "http://101.33.209.65:8081/upload";

void http_stream_upload(const uint8_t *jpeg_data, size_t len)
{
    esp_http_client_config_t cfg = {
        .url = UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 2000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client) {
        esp_http_client_set_post_field(client, (const char *)jpeg_data, len);
        esp_http_client_set_header(client, "Content-Type", "image/jpeg");
        esp_err_t err = esp_http_client_perform(client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "上传失败: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }
}
