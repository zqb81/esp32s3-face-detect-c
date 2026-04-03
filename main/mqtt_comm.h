/**
 * MQTT 通信
 */

#ifndef MQTT_COMM_H
#define MQTT_COMM_H

#include "esp_err.h"
#include "face_detect.h"
#include <stdint.h>

/**
 * 初始化并连接 MQTT
 */
esp_err_t mqtt_init(void);

/**
 * 发送检测结果
 */
esp_err_t mqtt_send_faces(const face_list_t *faces);

/**
 * 发送人脸裁剪图 (base64)
 */
esp_err_t mqtt_send_face_crop(const uint8_t *rgb565, int img_w, int img_h,
                              const face_result_t *face);

/**
 * 断开连接
 */
void mqtt_deinit(void);

#endif // MQTT_COMM_H
