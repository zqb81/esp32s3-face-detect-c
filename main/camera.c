/**
 * 摄像头驱动实现 - OV5640 via esp32-camera
 */

#include "camera.h"
#include "config.h"
#include "esp_log.h"

static const char *TAG = "camera";

esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_d0       = CAM_PIN_D0,
        .pin_d1       = CAM_PIN_D1,
        .pin_d2       = CAM_PIN_D2,
        .pin_d3       = CAM_PIN_D3,
        .pin_d4       = CAM_PIN_D4,
        .pin_d5       = CAM_PIN_D5,
        .pin_d6       = CAM_PIN_D6,
        .pin_d7       = CAM_PIN_D7,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_pclk     = CAM_PIN_PCLK,
        .pin_vsync    = CAM_PIN_VSYNC,
        .pin_href     = CAM_PIN_HREF,
        .pin_sccb_sda = CAM_PIN_SDA,
        .pin_sccb_scl = CAM_PIN_SCL,
        .pin_pwdn     = -1,
        .pin_reset    = -1,
        .xclk_freq_hz = CAM_XCLK_FREQ,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size   = FRAMESIZE_QVGA,  // 320x240
        .jpeg_quality = 12,
        .fb_count     = 2,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
        .fb_location  = CAMERA_FB_IN_PSRAM,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败: 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);    // 上下翻转
        s->set_hmirror(s, 0);
    }

    ESP_LOGI(TAG, "摄像头 OK (QVGA RGB565)");
    return ESP_OK;
}

camera_fb_t *camera_capture(void)
{
    return esp_camera_fb_get();
}

void camera_return(camera_fb_t *fb)
{
    esp_camera_fb_return(fb);
}

void camera_deinit(void)
{
    esp_camera_deinit();
}
