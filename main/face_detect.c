/**
 * 人脸检测实现 - esp-dl SCRFD
 * 
 * 注意：以下代码是框架，实际 API 调用需要根据 esp-dl 版本调整
 * esp-dl 文档：https://docs.espressif.com/projects/esp-dl/
 */

#include "face_detect.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "face_detect";

// esp-dl 头文件（根据实际版本调整）
// #include "dl_detect.hpp"
// #include "human_face_detect.hpp"

static bool s_initialized = false;

esp_err_t face_detect_init(void)
{
    // TODO: 加载 SCRFD 模型
    // 方案1: 使用 esp-dl 内置 HumanFaceDetect
    //   HumanFaceDetect *detect = new HumanFaceDetect();
    //
    // 方案2: 加载自定义 SCRFD 量化模型
    //   从 flash (model 分区) 加载 .espdl 模型文件

    ESP_LOGI(TAG, "人脸检测初始化...");
    
    // 初始化检测器
    // detect = new HumanFaceDetect();
    // 或
    // scrfd_model = new SCRFD();
    // scrfd_model->load("/spiffs/scrfd_int8.espdl");

    s_initialized = true;
    ESP_LOGI(TAG, "人脸检测 OK");
    return ESP_OK;
}

esp_err_t face_detect_run(const uint8_t *rgb565_buf, int width, int height,
                          face_list_t *results)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(results, 0, sizeof(face_list_t));

    // TODO: 调用 esp-dl 检测
    // std::vector<dl::detect::result_t> res = detect->run(rgb565_buf, width, height);
    //
    // for (auto &r : res) {
    //     face_result_t f;
    //     f.score = r.score;
    //     f.x1 = r.box[0]; f.y1 = r.box[1];
    //     f.x2 = r.box[2]; f.y2 = r.box[3];
    //     memcpy(f.landmarks, r.keypoint, sizeof(float) * 10);
    //     f.has_landmarks = true;
    //     ...
    // }

    // 临时：分配空结果（实际由检测器填充）
    results->faces = NULL;
    results->count = 0;
    results->capacity = 0;

    return ESP_OK;
}

void face_detect_free(face_list_t *results)
{
    if (results->faces) {
        free(results->faces);
        results->faces = NULL;
    }
    results->count = 0;
    results->capacity = 0;
}

void face_detect_deinit(void)
{
    s_initialized = false;
    ESP_LOGI(TAG, "人脸检测已关闭");
}
