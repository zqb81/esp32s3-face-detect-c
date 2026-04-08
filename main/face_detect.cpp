/**
 * Face detect wrapper for esp-dl human face models.
 */

#include "face_detect.h"

#include "dl_detect_base.hpp"
#include "dl_detect_mnp_postprocessor.hpp"
#include "dl_detect_msr_postprocessor.hpp"
#include "dl_image_define.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_model_base.hpp"
#include "esp_log.h"

#include <list>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

static const char *TAG = "face_detect";
static const char *kPartitionLabel = "model";
static const char *kMsrModelName = "face_msr.espdl";
static const char *kMnpModelName = "face_mnp.espdl";

class FaceDetectMSR : public dl::detect::DetectImpl {
public:
    FaceDetectMSR(const char *partition_label, const char *model_name, float score_thr, float nms_thr)
    {
        m_model = new dl::Model(partition_label, model_name, fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
        if (!m_model || !m_model->get_fbs_model()) {
            return;
        }
        m_model->minimize();
        m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, true);
        m_postprocessor =
            new dl::detect::MSRPostprocessor(m_model,
                                             m_image_preprocessor,
                                             score_thr,
                                             nms_thr,
                                             10,
                                             {{8, 8, 9, 9, {{16, 16}, {32, 32}}},
                                              {16, 16, 9, 9, {{64, 64}, {128, 128}}}});
    }

    bool ready() const
    {
        return m_model && m_model->get_fbs_model() && m_image_preprocessor && m_postprocessor;
    }
};

class FaceDetectMNP {
public:
    static constexpr float kDefaultScoreThr = 0.5f;
    static constexpr float kDefaultNmsThr = 0.5f;

    FaceDetectMNP(const char *partition_label, const char *model_name, float score_thr, float nms_thr)
    {
        m_model = new dl::Model(partition_label, model_name, fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
        if (!m_model || !m_model->get_fbs_model()) {
            return;
        }
        m_model->minimize();
        m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, true);
        m_postprocessor = new dl::detect::MNPPostprocessor(
            m_model, m_image_preprocessor, score_thr, nms_thr, 10, {{1, 1, 0, 0, {{48, 48}}}});
    }

    ~FaceDetectMNP()
    {
        delete m_model;
        delete m_image_preprocessor;
        delete m_postprocessor;
    }

    bool ready() const
    {
        return m_model && m_model->get_fbs_model() && m_image_preprocessor && m_postprocessor;
    }

    std::list<dl::detect::result_t> &run(const dl::image::img_t &img, std::list<dl::detect::result_t> &candidates)
    {
        m_postprocessor->clear_result();
        for (auto &candidate : candidates) {
            int center_x = (candidate.box[0] + candidate.box[2]) >> 1;
            int center_y = (candidate.box[1] + candidate.box[3]) >> 1;
            int side = DL_MAX(candidate.box[2] - candidate.box[0], candidate.box[3] - candidate.box[1]);
            candidate.box[0] = center_x - (side >> 1);
            candidate.box[1] = center_y - (side >> 1);
            candidate.box[2] = candidate.box[0] + side;
            candidate.box[3] = candidate.box[1] + side;
            candidate.limit_box(img.width, img.height);

            m_image_preprocessor->preprocess(img, candidate.box);
            m_model->run();
            m_postprocessor->postprocess();
        }
        m_postprocessor->nms();
        return m_postprocessor->get_result(img.width, img.height);
    }

private:
    dl::Model *m_model = nullptr;
    dl::image::ImagePreprocessor *m_image_preprocessor = nullptr;
    dl::detect::MNPPostprocessor *m_postprocessor = nullptr;
};

class FaceDetectMSRMNP : public dl::detect::Detect {
public:
    FaceDetectMSRMNP(const char *partition_label,
                     const char *msr_model_name,
                     float msr_score_thr,
                     float msr_nms_thr,
                     const char *mnp_model_name,
                     float mnp_score_thr,
                     float mnp_nms_thr) :
        m_msr(partition_label, msr_model_name, msr_score_thr, msr_nms_thr),
        m_mnp(partition_label, mnp_model_name, mnp_score_thr, mnp_nms_thr)
    {
    }

    bool ready() const
    {
        return m_msr.ready() && m_mnp.ready();
    }

    std::list<dl::detect::result_t> &run(const dl::image::img_t &img) override
    {
        std::list<dl::detect::result_t> &candidates = m_msr.run(img);
        return m_mnp.run(img, candidates);
    }

    Detect &set_score_thr(float score_thr, int idx) override
    {
        if (idx == 0) {
            m_msr.set_score_thr(score_thr);
        }
        return *this;
    }

    Detect &set_nms_thr(float nms_thr, int idx) override
    {
        if (idx == 0) {
            m_msr.set_nms_thr(nms_thr);
        }
        return *this;
    }

    dl::Model *get_raw_model(int idx) override
    {
        if (idx == 0) {
            return m_msr.get_raw_model();
        }
        return nullptr;
    }

private:
    FaceDetectMSR m_msr;
    FaceDetectMNP m_mnp;
};

static FaceDetectMSRMNP *s_detector = nullptr;

} // namespace

esp_err_t face_detect_init(void)
{
    ESP_LOGI(TAG, "Face detect init (MSR + MNP)");

    s_detector = new FaceDetectMSRMNP(kPartitionLabel,
                                      kMsrModelName,
                                      0.5f,
                                      0.5f,
                                      kMnpModelName,
                                      FaceDetectMNP::kDefaultScoreThr,
                                      FaceDetectMNP::kDefaultNmsThr);
    if (!s_detector || !s_detector->ready()) {
        ESP_LOGE(TAG,
                 "Human face models load failed: %s/%s and %s/%s",
                 kPartitionLabel,
                 kMsrModelName,
                 kPartitionLabel,
                 kMnpModelName);
        face_detect_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Human face models loaded: %s, %s", kMsrModelName, kMnpModelName);
    return ESP_OK;
}

esp_err_t face_detect_run(const uint8_t *rgb565_buf, int width, int height, face_list_t *results)
{
    memset(results, 0, sizeof(face_list_t));

    if (!s_detector) {
        return ESP_ERR_INVALID_STATE;
    }

    dl::image::img_t img = {
        .data = (void *)rgb565_buf,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE,
    };

    std::list<dl::detect::result_t> &det_results = s_detector->run(img);
    if (!det_results.empty()) {
        int count = det_results.size();
        results->faces = (face_result_t *)calloc(count, sizeof(face_result_t));
        results->capacity = count;

        for (const auto &r : det_results) {
            face_result_t *f = &results->faces[results->count];
            f->score = r.score;
            f->x1 = r.box[0];
            f->y1 = r.box[1];
            f->x2 = r.box[2];
            f->y2 = r.box[3];
            if (r.keypoint.size() >= 10) {
                f->has_landmarks = true;
                for (int i = 0; i < 10; i++) {
                    f->landmarks[i] = r.keypoint[i];
                }
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
    if (s_detector) {
        delete s_detector;
        s_detector = nullptr;
    }
    ESP_LOGI(TAG, "Face detect deinit");
}
