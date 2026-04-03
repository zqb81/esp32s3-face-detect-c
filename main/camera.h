/**
 * 摄像头驱动 - OV5640
 */

#ifndef CAMERA_H
#define CAMERA_H

#include "esp_err.h"
#include "esp_camera.h"

/**
 * 初始化摄像头
 * 配置引脚、时钟、分辨率 (QVGA RGB565)
 */
esp_err_t camera_init(void);

/**
 * 捕获一帧
 * @return camera_fb_t* 帧缓冲，用完后调用 camera_return()
 */
camera_fb_t *camera_capture(void);

/**
 * 归还帧缓冲
 */
void camera_return(camera_fb_t *fb);

/**
 * 反初始化
 */
void camera_deinit(void);

#endif // CAMERA_H
