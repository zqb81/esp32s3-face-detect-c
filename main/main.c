/**
 * ESP32-S3 face detect application.
 *
 * Core 0:
 *   capture JPEG -> copy into pool buffer -> queue -> return camera frame
 *
 * Core 1:
 *   decode JPEG -> update local stream -> run face detect -> display -> upload
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "camera.h"
#include "config.h"
#include "display.h"
#include "face_detect.h"
#include "http_stream.h"
#include "mqtt_comm.h"

static const char *TAG = "main";

#define POOL_COUNT 4
#define POOL_BUF_SIZE (100 * 1024)

static uint8_t *s_pool[POOL_COUNT] = {NULL};
static QueueHandle_t s_free_queue = NULL;
static QueueHandle_t s_work_queue = NULL;

typedef struct {
    uint8_t *data;
    size_t len;
} frame_msg_t;

typedef struct {
    uint32_t count;
    uint64_t total_us;
    uint32_t max_us;
} perf_bucket_t;

static void perf_bucket_add(perf_bucket_t *bucket, int64_t elapsed_us)
{
    if (!bucket || elapsed_us < 0) {
        return;
    }

    bucket->count++;
    bucket->total_us += (uint64_t)elapsed_us;
    if ((uint32_t)elapsed_us > bucket->max_us) {
        bucket->max_us = (uint32_t)elapsed_us;
    }
}

static float perf_bucket_avg_ms(const perf_bucket_t *bucket)
{
    if (!bucket || bucket->count == 0) {
        return 0.0f;
    }
    return (float)bucket->total_us / 1000.0f / (float)bucket->count;
}

static float perf_bucket_max_ms(const perf_bucket_t *bucket)
{
    if (!bucket) {
        return 0.0f;
    }
    return (float)bucket->max_us / 1000.0f;
}

static void perf_bucket_reset(perf_bucket_t *bucket)
{
    if (!bucket) {
        return;
    }
    bucket->count = 0;
    bucket->total_us = 0;
    bucket->max_us = 0;
}

static void face_list_replace(face_list_t *dst, const face_list_t *src)
{
    face_detect_free(dst);
    if (!src || src->count <= 0 || !src->faces) {
        return;
    }

    dst->faces = calloc(src->count, sizeof(face_result_t));
    if (!dst->faces) {
        return;
    }

    memcpy(dst->faces, src->faces, src->count * sizeof(face_result_t));
    dst->count = src->count;
    dst->capacity = src->count;
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "Connecting Wi-Fi: %s", WIFI_SSID);

    for (int i = 0; i < WIFI_MAX_RETRY; i++) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi connected");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG, "Wi-Fi connect timeout");
}

static void downsample_frame(const uint8_t *src,
                             uint8_t *dst,
                             int src_w,
                             int src_h,
                             int dst_w,
                             int dst_h)
{
    int src_stride = src_w * 2;
    int dst_off = 0;
    int src_y = 0;

    for (int row = 0; row < dst_h; row++) {
        int src_row = src_h - 1 - src_y;
        int src_row_off = src_row * src_stride;

        for (int col = 0; col < dst_w; col++) {
            int src_off = src_row_off + col * 4;
            dst[dst_off] = src[src_off];
            dst[dst_off + 1] = src[src_off + 1];
            dst_off += 2;
        }

        src_y += (row % 3 == 2) ? 2 : 1;
    }
}

static void draw_face_boxes(uint8_t *buf,
                            const face_list_t *faces,
                            int dst_w,
                            int dst_h,
                            int cam_w,
                            int cam_h)
{
    for (int i = 0; i < faces->count; i++) {
        const face_result_t *f = &faces->faces[i];
        float cx = (f->x1 + f->x2) / 2.0f * dst_w / cam_w;
        float cy = (dst_h - 1) - (f->y1 + f->y2) / 2.0f * dst_h / cam_h;
        float hw = (f->x2 - f->x1) * dst_w / cam_w / 2.0f * FACE_BOX_SCALE;
        float hh = (f->y2 - f->y1) * dst_h / cam_h / 2.0f * FACE_BOX_SCALE;

        int dx1 = (int)(cx - hw);
        int dy1 = (int)(cy - hh);
        int dx2 = (int)(cx + hw);
        int dy2 = (int)(cy + hh);

        if (dx1 < 0) {
            dx1 = 0;
        }
        if (dy1 < 0) {
            dy1 = 0;
        }
        if (dx2 >= dst_w) {
            dx2 = dst_w - 1;
        }
        if (dy2 >= dst_h) {
            dy2 = dst_h - 1;
        }

        uint16_t color = COLOR_RED;
        if (f->score > 0.8f) {
            color = COLOR_GREEN;
        } else if (f->score > 0.6f) {
            color = COLOR_YELLOW;
        }

        display_draw_rect(buf, dx1, dy1, dx2 - dx1, dy2 - dy1, color);
    }
}

static void capture_task(void *arg)
{
    ESP_LOGI(TAG, "Capture task start on core %d", xPortGetCoreID());

    while (1) {
        camera_fb_t *fb = camera_capture();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint8_t *buf = NULL;
        if (xQueueReceive(s_free_queue, &buf, 0) == pdTRUE) {
            if (fb->len <= POOL_BUF_SIZE) {
                memcpy(buf, fb->buf, fb->len);

                frame_msg_t msg = {
                    .data = buf,
                    .len = fb->len,
                };
                if (xQueueSend(s_work_queue, &msg, 0) != pdTRUE) {
                    xQueueSend(s_free_queue, &buf, 0);
                }
            } else {
                xQueueSend(s_free_queue, &buf, 0);
                ESP_LOGW(TAG, "JPEG too large: %zu > %d", fb->len, POOL_BUF_SIZE);
            }
        }

        camera_return(fb);
    }
}

static void process_task(void *arg)
{
    ESP_LOGI(TAG, "Process task start on core %d", xPortGetCoreID());

    uint8_t *rgb_buf = heap_caps_malloc(CAM_W * CAM_H * 2, MALLOC_CAP_SPIRAM);
    uint8_t *tft_buf = heap_caps_malloc(TFT_WIDTH * TFT_HEIGHT * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!rgb_buf || !tft_buf) {
        ESP_LOGE(TAG, "Process buffers allocation failed");
        free(rgb_buf);
        free(tft_buf);
        vTaskDelete(NULL);
        return;
    }

    int frame = 0;
    int fps_count = 0;
    int64_t fps_time = esp_timer_get_time();
    int last_jpeg_size = 0;
    face_list_t last_faces = {0};
    perf_bucket_t decode_perf = {0};
    perf_bucket_t downsample_perf = {0};
    perf_bucket_t detect_perf = {0};
    perf_bucket_t display_perf = {0};
    perf_bucket_t total_perf = {0};

    while (1) {
        frame_msg_t msg;
        if (xQueueReceive(s_work_queue, &msg, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }

        int64_t frame_start_us = esp_timer_get_time();
        last_jpeg_size = (int)msg.len;

        int out_w = 0;
        int out_h = 0;
        int64_t decode_start_us = esp_timer_get_time();
        esp_err_t dec_ret = camera_jpeg_to_rgb565(msg.data, msg.len, rgb_buf, &out_w, &out_h);
        perf_bucket_add(&decode_perf, esp_timer_get_time() - decode_start_us);

        http_stream_update_frame(msg.data, msg.len);
        http_stream_submit_upload(msg.data, msg.len);
        xQueueSend(s_free_queue, &msg.data, 0);

        if (dec_ret != ESP_OK || out_w <= 0 || out_h <= 0) {
            continue;
        }

        int64_t downsample_start_us = esp_timer_get_time();
        downsample_frame(rgb_buf, tft_buf, out_w, out_h, TFT_WIDTH, TFT_HEIGHT);
        perf_bucket_add(&downsample_perf, esp_timer_get_time() - downsample_start_us);

        face_list_t faces = {0};
        if (FACE_DETECT_INTERVAL_FRAMES > 0 && (frame % FACE_DETECT_INTERVAL_FRAMES) == 0) {
            int64_t detect_start_us = esp_timer_get_time();
            esp_err_t det_ret = face_detect_run(rgb_buf, out_w, out_h, &faces);
            perf_bucket_add(&detect_perf, esp_timer_get_time() - detect_start_us);
            if (det_ret != ESP_OK) {
                ESP_LOGW(TAG, "face_detect_run failed on frame %d: %s", frame, esp_err_to_name(det_ret));
            } else if (faces.count > 0) {
                mqtt_send_faces(&faces, frame, out_w, out_h);
                face_list_replace(&last_faces, &faces);

                const face_result_t *best = &faces.faces[0];
                for (int i = 1; i < faces.count; i++) {
                    if (faces.faces[i].score > best->score) {
                        best = &faces.faces[i];
                    }
                }

                if (best->score > FACE_SCORE_THRESHOLD) {
                    mqtt_send_face_crop(rgb_buf, out_w, out_h, best);
                }
            } else {
                face_detect_free(&last_faces);
            }
        }

        if (last_faces.count > 0) {
            draw_face_boxes(tft_buf, &last_faces, TFT_WIDTH, TFT_HEIGHT, out_w, out_h);
        }

        int64_t display_start_us = esp_timer_get_time();
        display_draw_frame(tft_buf);
        perf_bucket_add(&display_perf, esp_timer_get_time() - display_start_us);
        face_detect_free(&faces);

        frame++;
        fps_count++;
        perf_bucket_add(&total_perf, esp_timer_get_time() - frame_start_us);

        int64_t elapsed_ms = (esp_timer_get_time() - fps_time) / 1000;
        if (elapsed_ms >= PERF_LOG_INTERVAL_MS) {
            float fps = fps_count * 1000.0f / elapsed_ms;
            size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            UBaseType_t work_pending = uxQueueMessagesWaiting(s_work_queue);
            UBaseType_t free_avail = uxQueueMessagesWaiting(s_free_queue);

            ESP_LOGW(TAG,
                     "FPS:%.1f frame:%d jpeg:%dB PSRAM:%.0fK RAM:%.0fK queued:%d free:%d "
                     "dec:%.1f/%.1fms down:%.1f/%.1fms det:%.1f/%.1fms draw:%.1f/%.1fms total:%.1f/%.1fms",
                     fps,
                     frame,
                     last_jpeg_size,
                     psram_free / 1024.0f,
                     ram_free / 1024.0f,
                     work_pending,
                     free_avail,
                     perf_bucket_avg_ms(&decode_perf),
                     perf_bucket_max_ms(&decode_perf),
                     perf_bucket_avg_ms(&downsample_perf),
                     perf_bucket_max_ms(&downsample_perf),
                     perf_bucket_avg_ms(&detect_perf),
                     perf_bucket_max_ms(&detect_perf),
                     perf_bucket_avg_ms(&display_perf),
                     perf_bucket_max_ms(&display_perf),
                     perf_bucket_avg_ms(&total_perf),
                     perf_bucket_max_ms(&total_perf));

            fps_time = esp_timer_get_time();
            fps_count = 0;
            perf_bucket_reset(&decode_perf);
            perf_bucket_reset(&downsample_perf);
            perf_bucket_reset(&detect_perf);
            perf_bucket_reset(&display_perf);
            perf_bucket_reset(&total_perf);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Face Detect v0.4 ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();

    s_free_queue = xQueueCreate(POOL_COUNT, sizeof(uint8_t *));
    s_work_queue = xQueueCreate(POOL_COUNT, sizeof(frame_msg_t));
    if (!s_free_queue || !s_work_queue) {
        ESP_LOGE(TAG, "Queue allocation failed");
        return;
    }

    for (int i = 0; i < POOL_COUNT; i++) {
        s_pool[i] = heap_caps_malloc(POOL_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pool[i]) {
            ESP_LOGE(TAG, "Pool buffer %d allocation failed", i);
            return;
        }
        xQueueSend(s_free_queue, &s_pool[i], 0);
    }

    ESP_LOGI(TAG,
             "Buffer pool ready: %d x %dKB = %dKB",
             POOL_COUNT,
             POOL_BUF_SIZE / 1024,
             POOL_COUNT * POOL_BUF_SIZE / 1024);

    ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "display_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = face_detect_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "face_detect_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = mqtt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mqtt_init failed: %s", esp_err_to_name(ret));
    }

    ret = http_stream_start(HTTP_STREAM_PORT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http_stream_start failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "HTTP stream endpoint: http://<device-ip>:%d", HTTP_STREAM_PORT);
    ESP_LOGI(TAG, "Face models are loaded from raw partition label 'model'");

    xTaskCreatePinnedToCore(capture_task, "capture", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(process_task, "process", TASK_STACK_SIZE * 4, NULL, TASK_PRIORITY, NULL, 1);
}
