/**
 * 摄像头驱动 - OV5640 JPEG 模式
 */

#ifndef CAMERA_H
#define CAMERA_H

#include "esp_err.h"
#include "esp_camera.h"
#include <stddef.h>

/**
 * 初始化摄像头 (JPEG QVGA)
 */
esp_err_t camera_init(void);

/**
 * 捕获一帧 JPEG
 */
camera_fb_t *camera_capture(void);

/**
 * 归还帧缓冲
 */
void camera_return(camera_fb_t *fb);

/**
 * JPEG 解码为 RGB565
 * @param jpeg_data  JPEG 数据
 * @param jpeg_len   JPEG 数据长度
 * @param rgb_buf    输出 RGB565 buffer (预分配 CAM_W*CAM_H*2 字节)
 * @param width      输出宽度
 * @param height     输出高度
 */
esp_err_t camera_jpeg_to_rgb565(const uint8_t *jpeg_data, size_t jpeg_len,
                                 uint8_t *rgb_buf, int *width, int *height);

/**
 * 反初始化
 */
void camera_deinit(void);

#endif // CAMERA_H
