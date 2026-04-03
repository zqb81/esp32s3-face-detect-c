/**
 * 人脸检测 - esp-dl SCRFD
 */

#ifndef FACE_DETECT_H
#define FACE_DETECT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// 检测结果
typedef struct {
    float score;
    int x1, y1, x2, y2;
    float landmarks[10];  // 5个关键点 (x,y) 对
    bool has_landmarks;
} face_result_t;

// 检测结果列表
typedef struct {
    face_result_t *faces;
    int count;
    int capacity;
} face_list_t;

/**
 * 初始化人脸检测模型
 */
esp_err_t face_detect_init(void);

/**
 * 执行人脸检测
 * @param rgb565_buf RGB565 图像数据
 * @param width 图像宽度
 * @param height 图像高度
 * @param results 输出结果列表
 * @return ESP_OK on success
 */
esp_err_t face_detect_run(const uint8_t *rgb565_buf, int width, int height,
                          face_list_t *results);

/**
 * 释放检测结果
 */
void face_detect_free(face_list_t *results);

/**
 * 反初始化
 */
void face_detect_deinit(void);

#endif // FACE_DETECT_H
