/**
 * 人脸检测 - esp-dl v2.x (启用模型从 SPIFFS 分区加载)
 */

#include "face_detect.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

// esp-dl headers
#include "dl_detect_base.hpp"
#include "dl_detect_mnp_postprocessor.hpp"
#include "dl_image_define.hpp"
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"

static const char *TAG = "face_detect";

static dl::detect::Detect *s_detector = nullptr;
static dl::Model *s_model = nullptr;
static dl::image::ImagePreprocessor *s_pre = nullptr;
static dl::detect::MNPPostprocessor *s_post = nullptr;

esp_err_t face_detect_init(void)
{
    ESP_LOGI(TAG, "人脸检测初始化 (SPIFFS:model)");

    // 从 SPIFFS 分区加载模型 (part label: model, mounted at /model)
    // 文件名固定为 scrfd_face_detect.espdl
    const char *model_path = "/model/scrfd_face_detect.espdl";

    // 使用 esp-dl 的文件构造器
    try {
        s_model = new dl::Model(model_path);
        s_model->build(0, dl::MEMORY_MANAGER_GREEDY);

        // 典型 SCRFD 输入为 320x240（与 CAM_W/CAM_H 一致）
        s_pre = new dl::image::ImagePreprocessor(CAM_W, CAM_H,
                    dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
                    dl::image::DL_IMAGE_RESIZE_LINEAR,
                    dl::image::DL_IMAGE_COLOR_MODE_RGB);

        // MNP 后处理参数：score_thr, nms_thr, top_k, stages
        static const int stages[] = {8, 16, 32};
        s_post = new dl::detect::MNPPostprocessor(s_model, s_pre,
                    0.5f, 0.45f, 50, stages);

        s_detector = new dl::detect::DetectImpl(s_model, s_pre, s_post);
        s_detector->set_score_thr(0.5f);
        s_detector->set_nms_thr(0.45f);
        ESP_LOGI(TAG, "SCRFD 模型加载成功: %s", model_path);
    } catch (...) {
        ESP_LOGE(TAG, "SCRFD 模型加载失败（检查 /model/scrfd_face_detect.espdl 是否存在）");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t face_detect_run(const uint8_t *rgb565_buf, int width, int height,
                          face_list_t *results)
{
    memset(results, 0, sizeof(face_list_t));

    if (!s_detector) return ESP_ERR_INVALID_STATE;

    dl::image::img_t img = {
        .data = (void *)rgb565_buf,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
    };

    std::list<dl::detect::result_t> &det_results = s_detector->run(img);

    if (!det_results.empty()) {
        int count = det_results.size();
        results->faces = (face_result_t *)calloc(count, sizeof(face_result_t));
        results->capacity = count;

        for (auto &r : det_results) {
            face_result_t *f = &results->faces[results->count];
            f->score = r.score;
            f->x1 = r.box[0]; f->y1 = r.box[1];
            f->x2 = r.box[2]; f->y2 = r.box[3];
            if (r.keypoint.size() >= 10) {
                f->has_landmarks = true;
                for (int i = 0; i < 10; i++)
                    f->landmarks[i] = r.keypoint[i];
            }
            results->count++;
        }
    }

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
    if (s_detector) { delete s_detector; s_detector = nullptr; }
    if (s_post) { delete s_post; s_post = nullptr; }
    if (s_pre) { delete s_pre; s_pre = nullptr; }
    if (s_model) { delete s_model; s_model = nullptr; }
    ESP_LOGI(TAG, "人脸检测已关闭");
}
