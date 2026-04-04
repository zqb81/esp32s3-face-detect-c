/**
 * 人脸检测实现 - esp-dl HumanFaceDetect (MNP + MSR)
 * 
 * 使用乐鑫 esp-dl 库内置的人脸检测模型
 * 支持 RGB565 输入，返回人脸框 + 5个关键点
 */

#include "face_detect.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

// esp-dl C++ 头文件
#include "human_face_detect.hpp"

static const char *TAG = "face_detect";

static HumanFaceDetect *s_detector = NULL;

esp_err_t face_detect_init(void)
{
    ESP_LOGI(TAG, "人脸检测初始化...");

    /**
     * HumanFaceDetect 构造参数:
     * - score_threshold: 置信度阈值 (0.0~1.0)
     * - nms_threshold: NMS 阈值 (0.0~1.0)
     * - top_k: 最多检测数量
     */
    s_detector = new HumanFaceDetect(0.5f, 0.45f, 10);

    if (!s_detector) {
        ESP_LOGE(TAG, "检测器创建失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "人脸检测 OK (esp-dl HumanFaceDetect)");
    return ESP_OK;
}

esp_err_t face_detect_run(const uint8_t *rgb565_buf, int width, int height,
                          face_list_t *results)
{
    if (!s_detector) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(results, 0, sizeof(face_list_t));

    // 构造 esp-dl 图像结构
    dl::img::img_t img;
    img.data = (uint8_t *)rgb565_buf;
    img.width = width;
    img.height = height;
    img.pix_type = dl::img::DL_IMAGE_PIX_TYPE_RGB565;

    // 执行检测
    std::list<dl::detect::result_t> &detect_results = s_detector->run(img);

    if (detect_results.empty()) {
        return ESP_OK;
    }

    // 分配结果数组
    int count = detect_results.size();
    results->faces = (face_result_t *)calloc(count, sizeof(face_result_t));
    if (!results->faces) {
        ESP_LOGE(TAG, "结果数组分配失败");
        return ESP_ERR_NO_MEM;
    }
    results->capacity = count;
    results->count = 0;

    // 复制结果
    for (auto &r : detect_results) {
        face_result_t *f = &results->faces[results->count];
        f->score = r.score;
        f->x1 = (int)r.box[0];
        f->y1 = (int)r.box[1];
        f->x2 = (int)r.box[2];
        f->y2 = (int)r.box[3];

        // 关键点 (5个点，每个 x,y)
        if (r.keypoint.size() >= 10) {
            f->has_landmarks = true;
            for (int i = 0; i < 10; i++) {
                f->landmarks[i] = r.keypoint[i];
            }
        } else {
            f->has_landmarks = false;
        }

        results->count++;
    }

    ESP_LOGD(TAG, "检测到 %d 张人脸", results->count);
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
    if (s_detector) {
        delete s_detector;
        s_detector = NULL;
    }
    ESP_LOGI(TAG, "人脸检测已关闭");
}
