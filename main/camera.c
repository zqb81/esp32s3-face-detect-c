/**
 * 摄像头驱动实现 - OV5640 JPEG 输出
 * JPEG 捕获 → 解码 RGB565 → 本地处理 + 上传
 */

#include "camera.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"  // esp_jpeg 组件
#include <string.h>

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
        .pixel_format = PIXFORMAT_JPEG,       // JPEG 输出
        .frame_size   = FRAMESIZE_QVGA,        // 320x240
        .jpeg_quality = 12,                     // 1-63，越小质量越高
        .fb_count     = 2,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
        .fb_location  = CAMERA_FB_IN_PSRAM,    // PSRAM 存帧
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败: 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 0);
        s->set_hmirror(s, 0);
    }

    ESP_LOGI(TAG, "摄像头 OK (JPEG QVGA)");
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

/**
 * JPEG 解码为 RGB565
 * @param jpeg_data  JPEG 数据
 * @param jpeg_len   JPEG 数据长度
 * @param rgb_buf    输出 RGB565 buffer (需预分配 320*240*2 = 153600 字节)
 * @param width      输出宽度
 * @param height     输出高度
 * @return ESP_OK on success
 */
esp_err_t camera_jpeg_to_rgb565(const uint8_t *jpeg_data, size_t jpeg_len,
                                 uint8_t *rgb_buf, int *width, int *height)
{
    // esp_jpeg 解码器配置
    esp_jpeg_image_cfg_t cfg = {
        .indata = jpeg_data,
        .indata_size = jpeg_len,
        .outbuf = rgb_buf,
        .outbuf_size = CAM_W * CAM_H * 2,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            .swap_color_bytes = 1,
        }
    };

    esp_jpeg_image_output_t out_info = {0};
    esp_err_t ret = esp_jpeg_decode(&cfg, &out_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG 解码失败: 0x%x", ret);
        return ret;
    }

    *width = out_info.width;
    *height = out_info.height;
    return ESP_OK;
}

void camera_deinit(void)
{
    esp_camera_deinit();
}
