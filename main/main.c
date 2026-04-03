/**
 * ESP32-S3 人脸检测 - 双核并行架构
 *
 * Core 0: 摄像头采集 → JPEG解码 → 人脸检测 → TFT显示
 * Core 1: JPEG帧上传 → 云端中转服务器
 *
 * 帧数据通过帧复制分发，避免 fb_return 竞争
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "config.h"
#include "camera.h"
#include "display.h"
#include "face_detect.h"
#include "mqtt_comm.h"
#include "http_stream.h"

static const char *TAG = "main";

// ===== 上传帧队列 =====
typedef struct {
    uint8_t *data;   // JPEG 数据副本 (malloc 分配)
    size_t   len;    // 数据长度
} upload_frame_t;

static QueueHandle_t s_upload_queue = NULL;

// ===== WiFi =====
static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    ESP_LOGI(TAG, "WiFi 连接中: %s", WIFI_SSID);

    for (int i = 0; i < WIFI_MAX_RETRY; i++) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi OK");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGW(TAG, "WiFi 连接超时");
}

// ===== 下采样 RGB565 → TFT =====
static void downsample_frame(const uint8_t *src, uint8_t *dst,
                              int src_w, int src_h, int dst_w, int dst_h)
{
    int src_stride = src_w * 2;
    int dst_off = 0;
    int src_y = 0;

    for (int row = 0; row < dst_h; row++) {
        int src_row = src_h - 1 - src_y;
        int src_row_off = src_row * src_stride;

        for (int col = 0; col < dst_w; col++) {
            int src_off = src_row_off + col * 4;
            dst[dst_off]     = src[src_off];
            dst[dst_off + 1] = src[src_off + 1];
            dst_off += 2;
        }

        if (row % 3 == 2)
            src_y += 2;
        else
            src_y += 1;
    }
}

// ===== 画人脸框 =====
static void draw_face_boxes(uint8_t *buf, const face_list_t *faces,
                             int dst_w, int dst_h, int cam_w, int cam_h)
{
    for (int i = 0; i < faces->count; i++) {
        const face_result_t *f = &faces->faces[i];
        float cx = (f->x1 + f->x2) / 2.0f * dst_w / cam_w;
        float cy = (dst_h - 1) - (f->y1 + f->y2) / 2.0f * dst_h / cam_h;
        float hw = (f->x2 - f->x1) * dst_w / cam_w / 2.0f * FACE_BOX_SCALE;
        float hh = (f->y2 - f->y1) * dst_h / cam_h / 2.0f * FACE_BOX_SCALE;

        int dx1 = (int)(cx - hw); if (dx1 < 0) dx1 = 0;
        int dy1 = (int)(cy - hh); if (dy1 < 0) dy1 = 0;
        int dx2 = (int)(cx + hw); if (dx2 >= dst_w) dx2 = dst_w - 1;
        int dy2 = (int)(cy + hh); if (dy2 >= dst_h) dy2 = dst_h - 1;

        uint16_t color;
        if (f->score > 0.8f)      color = COLOR_GREEN;
        else if (f->score > 0.6f) color = COLOR_YELLOW;
        else                       color = COLOR_RED;

        display_draw_rect(buf, dx1, dy1, dx2 - dx1, dy2 - dy1, color);
    }
}

// ============================================================
// Core 1: 上传任务
// 从队列取 JPEG 帧 → POST 到云端 → 释放内存
// ============================================================
static void upload_task(void *arg)
{
    ESP_LOGI(TAG, "上传任务启动 (Core %d)", xPortGetCoreID());

    upload_frame_t frame;
    while (1) {
        if (xQueueReceive(s_upload_queue, &frame, pdMS_TO_TICKS(500)) == pdTRUE) {
            http_stream_upload(frame.data, frame.len);
            free(frame.data);  // 释放副本
        }
    }

    vTaskDelete(NULL);
}

// ============================================================
// Core 0: 主任务
// 采集 → 解码 → 检测 → TFT → 复制JPEG给上传队列
// ============================================================
static void main_task(void *arg)
{
    ESP_LOGI(TAG, "主任务启动 (Core %d)", xPortGetCoreID());

    // 分配缓冲区
    uint8_t *tft_buf = heap_caps_malloc(TFT_WIDTH * TFT_HEIGHT * 2, MALLOC_CAP_DMA);
    uint8_t *rgb_buf = heap_caps_malloc(CAM_W * CAM_H * 2, MALLOC_CAP_SPIRAM);

    if (!tft_buf || !rgb_buf) {
        ESP_LOGE(TAG, "缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }

    int frame = 0;
    int fps_count = 0;
    int64_t fps_time = esp_timer_get_time();
    int last_jpeg_size = 0;
    int dropped = 0;

    while (1) {
        // 1. 捕获 JPEG
        camera_fb_t *fb = camera_capture();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        last_jpeg_size = fb->len;

        // 2. 帧复制 → 上传队列 (Core 1 处理)
        uint8_t *jpeg_copy = malloc(fb->len);
        if (jpeg_copy) {
            memcpy(jpeg_copy, fb->buf, fb->len);
            upload_frame_t uf = { .data = jpeg_copy, .len = fb->len };
            if (xQueueSend(s_upload_queue, &uf, 0) != pdTRUE) {
                // 队列满，丢弃
                free(jpeg_copy);
                dropped++;
            }
        }

        // 3. JPEG → RGB565 解码
        int out_w = 0, out_h = 0;
        esp_err_t dec_ret = camera_jpeg_to_rgb565(fb->buf, fb->len,
                                                   rgb_buf, &out_w, &out_h);

        // 4. 释放原始帧（重要！不释放会内存泄漏）
        camera_return(fb);

        if (dec_ret != ESP_OK || out_w <= 0) {
            continue;
        }

        // 5. 下采样到 TFT
        downsample_frame(rgb_buf, tft_buf, out_w, out_h, TFT_WIDTH, TFT_HEIGHT);

        // 6. 人脸检测（每 5 帧）
        face_list_t faces = {0};
        if (frame % 5 == 0) {
            face_detect_run(rgb_buf, out_w, out_h, &faces);
            if (faces.count > 0) {
                mqtt_send_faces(&faces);
                // 上传最佳人脸裁剪
                const face_result_t *best = &faces.faces[0];
                for (int i = 1; i < faces.count; i++) {
                    if (faces.faces[i].score > best->score)
                        best = &faces.faces[i];
                }
                if (best->score > FACE_SCORE_THRESHOLD) {
                    mqtt_send_face_crop(rgb_buf, out_w, out_h, best);
                }
            }
        }

        // 7. 画人脸框 + 显示
        if (faces.count > 0) {
            draw_face_boxes(tft_buf, &faces, TFT_WIDTH, TFT_HEIGHT, out_w, out_h);
        }
        display_draw_frame(tft_buf);
        face_detect_free(&faces);

        // FPS 统计
        frame++;
        fps_count++;
        int64_t elapsed = (esp_timer_get_time() - fps_time) / 1000;
        if (elapsed >= 1000) {
            float fps = fps_count * 1000.0f / elapsed;
            size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            UBaseType_t qfree = uxQueueSpacesAvailable(s_upload_queue);
            ESP_LOGI(TAG, "FPS:%.1f 帧:%d JPEG:%dB PSRAM:%.0fK RAM:%.0fK 丢帧:%d 队列空:%d",
                     fps, frame, last_jpeg_size,
                     free_psram / 1024.0f, free_ram / 1024.0f,
                     dropped, (int)qfree);
            fps_time = esp_timer_get_time();
            fps_count = 0;
            dropped = 0;
        }
    }

    free(tft_buf);
    free(rgb_buf);
    vTaskDelete(NULL);
}

// ============================================================
// app_main
// ============================================================
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 人脸检测 v0.3 (双核并行) ===");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi
    wifi_init();

    // 上传队列 (最多缓冲 3 帧)
    s_upload_queue = xQueueCreate(3, sizeof(upload_frame_t));

    // 模块初始化
    display_init();
    camera_init();
    face_detect_init();
    mqtt_init();
    http_stream_start(HTTP_STREAM_PORT);

    // Core 1: 上传任务
    xTaskCreatePinnedToCore(upload_task, "upload", 4096,
                            NULL, 3, NULL, 1);

    // Core 0: 主任务 (大栈，JPEG 解码需要)
    xTaskCreatePinnedToCore(main_task, "main", TASK_STACK_SIZE * 4,
                            NULL, TASK_PRIORITY, NULL, 0);
}
