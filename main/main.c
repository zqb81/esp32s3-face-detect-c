/**
 * ESP32-S3 人脸检测 v0.4 - Buffer Pool + 双核优化架构
 *
 * Core 0: 摄像头采集 → 复制到 pool buffer → 入队 → 立即 fb_return()
 * Core 1: 取 buffer → JPEG解码 → 人脸检测 → TFT显示 → 上传 → 归还 pool
 *
 * 关键优化:
 * - 预分配 buffer 池，零 malloc/free
 * - Core 0 只做 capture+copy+return，<5ms
 * - 队列满自动丢帧
 * - Core 1 统一处理解码/检测/显示/上传
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

// ===== Buffer Pool =====
#define POOL_COUNT      4           // 预分配 4 个 buffer
#define POOL_BUF_SIZE   (100 * 1024) // 每个 100KB (QVGA JPEG 通常 <30KB)

// PSRAM 分配，不在内部 DRAM
static uint8_t *s_pool[POOL_COUNT] = {NULL};
static QueueHandle_t s_free_queue = NULL;   // 空闲 buffer 指针队列
static QueueHandle_t s_work_queue = NULL;   // 待处理 buffer 指针队列

typedef struct {
    uint8_t *data;
    size_t   len;
} frame_msg_t;

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
// Core 0: 采集任务（极轻量）
// capture → 从 pool 取 buf → memcpy → 入队 → fb_return
// ============================================================
static void capture_task(void *arg)
{
    ESP_LOGI(TAG, "采集任务启动 (Core %d)", xPortGetCoreID());

    int frame = 0;
    int dropped = 0;

    while (1) {
        // 1. 捕获 JPEG
        camera_fb_t *fb = camera_capture();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // 2. 从 pool 取空闲 buffer（非阻塞，没有就丢帧）
        uint8_t *buf = NULL;
        if (xQueueReceive(s_free_queue, &buf, 0) == pdTRUE) {
            // 3. 复制 JPEG 到 pool buffer
            if (fb->len <= POOL_BUF_SIZE) {
                memcpy(buf, fb->buf, fb->len);

                // 4. 入工作队列（非阻塞，满了也丢帧）
                frame_msg_t msg = { .data = buf, .len = fb->len };
                if (xQueueSend(s_work_queue, &msg, 0) != pdTRUE) {
                    // 队列满，归还 buffer
                    xQueueSend(s_free_queue, &buf, 0);
                    dropped++;
                }
            } else {
                // JPEG 太大，归还 buffer
                xQueueSend(s_free_queue, &buf, 0);
                ESP_LOGW(TAG, "JPEG 超大: %zu > %d", fb->len, POOL_BUF_SIZE);
            }
        } else {
            dropped++;
        }

        // 5. 立即归还摄像头帧（关键！不阻塞采集）
        camera_return(fb);

        frame++;
    }

    vTaskDelete(NULL);
}

// ============================================================
// Core 1: 处理任务（解码 + 检测 + 显示 + 上传）
// ============================================================
static void process_task(void *arg)
{
    ESP_LOGI(TAG, "处理任务启动 (Core %d)", xPortGetCoreID());

    // 分配 RGB565 解码 buffer (PSRAM)
    uint8_t *rgb_buf = heap_caps_malloc(CAM_W * CAM_H * 2, MALLOC_CAP_SPIRAM);
    uint8_t *tft_buf = heap_caps_malloc(TFT_WIDTH * TFT_HEIGHT * 2, MALLOC_CAP_DMA);

    if (!rgb_buf || !tft_buf) {
        ESP_LOGE(TAG, "处理缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }

    int frame = 0;
    int fps_count = 0;
    int64_t fps_time = esp_timer_get_time();
    int last_jpeg_size = 0;

    while (1) {
        frame_msg_t msg;
        if (xQueueReceive(s_work_queue, &msg, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }

        last_jpeg_size = msg.len;

        // 1. JPEG → RGB565 解码
        int out_w = 0, out_h = 0;
        esp_err_t dec_ret = camera_jpeg_to_rgb565(msg.data, msg.len,
                                                   rgb_buf, &out_w, &out_h);

        // 2. 上传到云端
        http_stream_upload(msg.data, msg.len);

        // 3. 归还 pool buffer（重要！采集任务才能复用）
        xQueueSend(s_free_queue, &msg.data, 0);

        if (dec_ret != ESP_OK || out_w <= 0) {
            continue;
        }

        // 4. 下采样到 TFT
        downsample_frame(rgb_buf, tft_buf, out_w, out_h, TFT_WIDTH, TFT_HEIGHT);

        // 5. 人脸检测（每 5 帧）
        face_list_t faces = {0};
        if (frame % 5 == 0) {
            face_detect_run(rgb_buf, out_w, out_h, &faces);
            if (faces.count > 0) {
                mqtt_send_faces(&faces);
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

        // 6. 画框 + 显示
        if (faces.count > 0) {
            draw_face_boxes(tft_buf, &faces, TFT_WIDTH, TFT_HEIGHT, out_w, out_h);
        }
        display_draw_frame(tft_buf);
        face_detect_free(&faces);

        // FPS
        frame++;
        fps_count++;
        int64_t elapsed = (esp_timer_get_time() - fps_time) / 1000;
        if (elapsed >= 1000) {
            float fps = fps_count * 1000.0f / elapsed;
            size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            UBaseType_t work_pending = uxQueueMessagesWaiting(s_work_queue);
            UBaseType_t free_avail = uxQueueMessagesWaiting(s_free_queue);
            ESP_LOGI(TAG, "FPS:%.1f 帧:%d JPEG:%dB PSRAM:%.0fK RAM:%.0fK 待处理:%d 空闲池:%d",
                     fps, frame, last_jpeg_size,
                     psram_free / 1024.0f, ram_free / 1024.0f,
                     work_pending, free_avail);
            fps_time = esp_timer_get_time();
            fps_count = 0;
        }
    }

    free(rgb_buf);
    free(tft_buf);
    vTaskDelete(NULL);
}

// ============================================================
// app_main
// ============================================================
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 人脸检测 v0.4 (Buffer Pool + 双核) ===");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi
    wifi_init();

    // 初始化 buffer pool
    s_free_queue = xQueueCreate(POOL_COUNT, sizeof(uint8_t *));
    s_work_queue = xQueueCreate(POOL_COUNT, sizeof(frame_msg_t));

    // 从 PSRAM 分配 buffer，放入空闲队列
    for (int i = 0; i < POOL_COUNT; i++) {
        s_pool[i] = heap_caps_malloc(POOL_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pool[i]) {
            ESP_LOGE(TAG, "Pool buffer %d 分配失败", i);
        }
        xQueueSend(s_free_queue, &s_pool[i], 0);
    }
    ESP_LOGI(TAG, "Buffer Pool: %d x %dKB = %dKB",
             POOL_COUNT, POOL_BUF_SIZE / 1024, POOL_COUNT * POOL_BUF_SIZE / 1024);

    // 模块初始化
    display_init();
    camera_init();
    face_detect_init();
    mqtt_init();
    http_stream_start(HTTP_STREAM_PORT);

    ESP_LOGI(TAG, "视频流: http://<IP>:%d", HTTP_STREAM_PORT);

    // Core 0: 采集（高优先级，不阻塞）
    xTaskCreatePinnedToCore(capture_task, "capture", 4096,
                            NULL, 5, NULL, 0);

    // Core 1: 处理（解码+检测+显示+上传）
    xTaskCreatePinnedToCore(process_task, "process", TASK_STACK_SIZE * 4,
                            NULL, TASK_PRIORITY, NULL, 1);
}
