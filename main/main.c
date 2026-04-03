/**
 * ESP32-S3 人脸检测 - 主入口
 * 摄像头→人脸检测→TFT显示→MQTT上报→HTTP视频流
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "camera.h"
#include "display.h"
#include "face_detect.h"
#include "mqtt_comm.h"
#include "http_stream.h"

static const char *TAG = "main";

// ===== WiFi 初始化 =====
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

    ESP_LOGI(TAG, "WiFi connecting to %s ...", WIFI_SSID);
    esp_wifi_connect();

    // 等待连接
    int retry = 0;
    while (retry < WIFI_MAX_RETRY) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi OK: %s", ap_info.ssid);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }
}

// ===== 下采样：QVGA → TFT =====
static void downsample_frame(const uint8_t *src, uint8_t *dst,
                              int src_w, int src_h, int dst_w, int dst_h)
{
    int src_stride = src_w * 2;
    int dst_off = 0;
    int src_y = 0;

    for (int row = 0; row < dst_h; row++) {
        int src_row = src_h - 1 - src_y;  // 上下翻转
        int src_row_off = src_row * src_stride;

        for (int col = 0; col < dst_w; col++) {
            int src_off = src_row_off + col * 4;
            dst[dst_off]     = src[src_off];
            dst[dst_off + 1] = src[src_off + 1];
            dst_off += 2;
        }

        // Y 方向：240→160，交替跳行
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

        // 选择颜色
        uint16_t color;
        if (f->score > 0.8f)      color = COLOR_GREEN;
        else if (f->score > 0.6f) color = COLOR_YELLOW;
        else                       color = COLOR_RED;

        display_draw_rect(buf, dx1, dy1, dx2 - dx1, dy2 - dy1, color);
    }
}

// ===== 主任务 =====
static void main_task(void *arg)
{
    ESP_LOGI(TAG, "=== ESP32-S3 人脸检测 C 版本 ===");

    // TFT buffer
    uint8_t *tft_buf = heap_caps_malloc(TFT_WIDTH * TFT_HEIGHT * 2, MALLOC_CAP_DMA);
    if (!tft_buf) {
        ESP_LOGE(TAG, "TFT buffer 分配失败");
        vTaskDelete(NULL);
        return;
    }

    int frame = 0;
    int fps_count = 0;
    int64_t fps_time = esp_timer_get_time();

    while (1) {
        // 1. 捕获
        camera_fb_t *fb = camera_capture();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 2. 下采样到 TFT
        downsample_frame(fb->buf, tft_buf, 320, 240, TFT_WIDTH, TFT_HEIGHT);

        // 3. 人脸检测（每 N 帧）
        face_list_t faces = {0};
        if (frame % 5 == 0) {
            esp_err_t ret = face_detect_run(fb->buf, 320, 240, &faces);
            if (ret == ESP_OK && faces.count > 0) {
                mqtt_send_faces(&faces);

                // 上传最佳人脸
                const face_result_t *best = &faces.faces[0];
                for (int i = 1; i < faces.count; i++) {
                    if (faces.faces[i].score > best->score)
                        best = &faces.faces[i];
                }
                if (best->score > FACE_SCORE_THRESHOLD) {
                    mqtt_send_face_crop(fb->buf, 320, 240, best);
                }
            }
        }

        // 4. 画人脸框
        if (faces.count > 0) {
            draw_face_boxes(tft_buf, &faces, TFT_WIDTH, TFT_HEIGHT, 320, 240);
        }

        // 5. 显示
        display_draw_frame(tft_buf);

        // 6. HTTP 视频流（JPEG 编码）
        // TODO: JPEG 编码 fb->buf → http_stream_update_frame()

        // 释放帧
        camera_return(fb);
        face_detect_free(&faces);

        // FPS 统计
        frame++;
        fps_count++;
        int64_t elapsed = (esp_timer_get_time() - fps_time) / 1000;
        if (elapsed >= 1000) {
            float fps = fps_count * 1000.0f / elapsed;
            ESP_LOGI(TAG, "FPS: %.1f | 帧: %d", fps, frame);
            fps_time = esp_timer_get_time();
            fps_count = 0;
        }
    }

    free(tft_buf);
    vTaskDelete(NULL);
}

// ===== app_main =====
void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi
    wifi_init();

    // 各模块初始化
    display_init();
    camera_init();
    face_detect_init();
    mqtt_init();
    http_stream_start(HTTP_STREAM_PORT);

    ESP_LOGI(TAG, "视频流: http://<IP>:%d/stream", HTTP_STREAM_PORT);

    // 启动主循环
    xTaskCreatePinnedToCore(main_task, "main", TASK_STACK_SIZE * 2,
                            NULL, TASK_PRIORITY, NULL, 0);
}
