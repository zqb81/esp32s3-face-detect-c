/**
 * 人脸检测 - esp-dl v2.x (需要下载 SCRFD 模型)
 *
 * 当前为 stub 实现，编译通过，不执行检测。
 * 下载模型后取消注释 TODO 部分即可启用。
 *
 * 模型获取方式:
 * 1. 从 esp-dl GitHub 仓库 examples 中获取预编译 .espdl 模型
 * 2. 下载 SCRFD face detection 模型
 * 3. 放到 main/models/ 目录，通过 PARTITION 或 RODATA 加载
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

static const char *TAG = "face_detect";

// TODO: 加载模型后取消注释
// static dl::detect::Detect *s_detector = nullptr;

esp_err_t face_detect_init(void)
{
    ESP_LOGI(TAG, "人脸检测初始化 (stub 模式 - 需要下载模型)");

    // TODO: 加载 SCRFD 模型
    // 方式1: 从 RODATA (模型编译进固件)
    //   dl::Model *model = new dl::Model((const char *)model_data_rodata);
    //   model->build(0, dl::MEMORY_MANAGER_GREEDY);
    //
    // 方式2: 从 Flash 分区
    //   dl::Model *model = new dl::Model("model", dl::fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
    //   model->build(0, dl::MEMORY_MANAGER_GREEDY);
    //
    // 配置预处理器
    // dl::image::ImagePreprocessor *preprocessor = new dl::image::ImagePreprocessor(...);
    //
    // 配置 MNP 后处理器
    // dl::detect::MNPPostprocessor *postprocessor = new dl::detect::MNPPostprocessor(
    //     model, preprocessor, 0.5f, 0.45f, 10, stages);
    //
    // s_detector = new dl::detect::DetectImpl(model, preprocessor, postprocessor);
    // s_detector->set_score_thr(0.5f);
    // s_detector->set_nms_thr(0.45f);

    ESP_LOGW(TAG, "人脸检测未启用 - 需要下载模型文件");
    return ESP_OK;
}

esp_err_t face_detect_run(const uint8_t *rgb565_buf, int width, int height,
                          face_list_t *results)
{
    memset(results, 0, sizeof(face_list_t));

    // TODO: 取消注释以下代码以启用检测
    // if (!s_detector) return ESP_ERR_INVALID_STATE;
    //
    // dl::image::img_t img = {
    //     .data = (void *)rgb565_buf,
    //     .width = (uint16_t)width,
    //     .height = (uint16_t)height,
    //     .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
    // };
    //
    // std::list<dl::detect::result_t> &det_results = s_detector->run(img);
    //
    // if (!det_results.empty()) {
    //     int count = det_results.size();
    //     results->faces = (face_result_t *)calloc(count, sizeof(face_result_t));
    //     results->capacity = count;
    //
    //     for (auto &r : det_results) {
    //         face_result_t *f = &results->faces[results->count];
    //         f->score = r.score;
    //         f->x1 = r.box[0]; f->y1 = r.box[1];
    //         f->x2 = r.box[2]; f->y2 = r.box[3];
    //         if (r.keypoint.size() >= 10) {
    //             f->has_landmarks = true;
    //             for (int i = 0; i < 10; i++)
    //                 f->landmarks[i] = r.keypoint[i];
    //         }
    //         results->count++;
    //     }
    // }

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
    // TODO: if (s_detector) { delete s_detector; s_detector = nullptr; }
    ESP_LOGI(TAG, "人脸检测已关闭");
}
