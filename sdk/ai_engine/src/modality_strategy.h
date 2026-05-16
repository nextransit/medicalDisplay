#ifndef MEDICALDISPLAY_MODALITY_STRATEGY_H
#define MEDICALDISPLAY_MODALITY_STRATEGY_H

#include "ai_engine.h"
#include "onnx_backend.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace medical_display {

float* preprocess_dicom(const uint16_t* data, int w, int h, int bits, int target_w, int target_h);
float* preprocess_image(const uint8_t* data, int w, int h, int channels, int target_w, int target_h);
void normalize_tensor(float* data, int size, float mean, float std);
uint64_t hash_metadata_key(const char* modality, const char* series, int body_part);

/**
 * ModalityClassifier - ONNX Runtime based modality classification with fallback
 *
 * Uses ONNX Runtime for AI inference when:
 * - AI_BACKEND_ONNXRUNTIME is defined (ONNX Runtime available)
 * - Model file is specified in config.model_path
 *
 * Falls back to rule-based detection when ONNX model unavailable.
 */
class ModalityClassifier {
public:
    static std::unique_ptr<ModalityClassifier> Create(const AIEngineConfig& config) {
        auto classifier = std::unique_ptr<ModalityClassifier>(new ModalityClassifier(config));

        // Try to create ONNX backend if model path is configured
        if (config.model_path[0] != '\0') {
            classifier->onnx_backend_ = ONNXBackend::Create(config.model_path, config.use_gpu);
        }

        return classifier;
    }

    /**
     * Run inference to classify modality
     * @param input_buffer Preprocessed input tensor
     * @param input_size Size of input buffer
     * @param scores Output scores for each modality
     * @param score_count Number of modalities (should be MODALITY_COUNT)
     * @return 0 success, -1 failure (will use fallback)
     */
    int Run(const float* input_buffer, int input_size, float* scores, int score_count) {
        if (!input_buffer || !scores || score_count <= 0) {
            return -1;
        }

        // Try ONNX inference first
        if (onnx_backend_ && onnx_backend_->IsValid()) {
            std::vector<float> output(score_count, 0.0f);
            int ret = onnx_backend_->Run(input_buffer, input_size, output.data(), score_count);
            if (ret == 0) {
                std::copy(output.begin(), output.begin() + score_count, scores);
                return 0;
            }
        }

        // Fallback to rule-based detection
        return RunRuleBased(input_buffer, scores, score_count);
    }

    bool HasONNXModel() const { return onnx_backend_ && onnx_backend_->IsValid(); }

private:
    explicit ModalityClassifier(const AIEngineConfig& config) : config_(config) {}

    /**
     * Rule-based fallback detection based on image statistics
     */
    int RunRuleBased(const float* input_buffer, float* scores, int score_count) {
        if (!input_buffer || !scores || score_count <= 0) {
            return -1;
        }

        std::fill(scores, scores + score_count, 0.01f);

        int sample_count = config_.input_width * config_.input_height;
        if (sample_count <= 0) {
            sample_count = 1;
        }

        float mean = 0.0f;
        for (int i = 0; i < sample_count; ++i) {
            mean += input_buffer[i];
        }
        mean /= static_cast<float>(sample_count);

        int preferred = MODALITY_CT;
        if (mean > 1.0f && MODALITY_MR < score_count) {
            preferred = MODALITY_MR;
        } else if (mean < -0.5f && MODALITY_DX < score_count) {
            preferred = MODALITY_DX;
        } else if (MODALITY_US < score_count && mean > 0.4f && mean <= 1.0f) {
            preferred = MODALITY_US;
        }

        scores[preferred] = 0.92f;
        if (MODALITY_CT < score_count && preferred != MODALITY_CT) {
            scores[MODALITY_CT] = 0.45f;
        }
        if (MODALITY_MR < score_count && preferred != MODALITY_MR) {
            scores[MODALITY_MR] = 0.37f;
        }
        if (MODALITY_US < score_count && preferred != MODALITY_US) {
            scores[MODALITY_US] = 0.24f;
        }

        return 0;
    }

    AIEngineConfig config_;
    std::unique_ptr<ONNXBackend> onnx_backend_;
};

class ModelRunner {
public:
    static std::unique_ptr<ModelRunner> Create(const AIEngineConfig& config) {
        return std::unique_ptr<ModelRunner>(new ModelRunner(config));
    }

    int Run(const float* input_buffer, float* scores, int score_count) {
        if (!input_buffer || !scores || score_count <= 0) {
            return -1;
        }

        // Use ONNX classifier with fallback to rule-based
        if (!classifier_) {
            classifier_ = ModalityClassifier::Create(config_);
        }

        if (classifier_) {
            return classifier_->Run(input_buffer, config_.input_width * config_.input_height,
                                    scores, score_count);
        }

        return -1;
    }

    bool HasONNXModel() const { return classifier_ && classifier_->HasONNXModel(); }

private:
    explicit ModelRunner(const AIEngineConfig& config) : config_(config) {}

    AIEngineConfig config_;
    std::unique_ptr<ModalityClassifier> classifier_;
};

class MetadataCache {
public:
    explicit MetadataCache(size_t capacity) : capacity_(capacity) {}

    bool Get(uint64_t key, AIRecognitionResult* result) {
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return false;
        }

        order_.erase(it->second.second);
        order_.push_front(key);
        it->second.second = order_.begin();

        if (result) {
            *result = it->second.first;
        }
        return true;
    }

    void Put(uint64_t key, const AIRecognitionResult* result) {
        if (!result || capacity_ == 0) {
            return;
        }

        auto existing = cache_.find(key);
        if (existing != cache_.end()) {
            order_.erase(existing->second.second);
            cache_.erase(existing);
        } else if (cache_.size() >= capacity_) {
            uint64_t stale_key = order_.back();
            order_.pop_back();
            cache_.erase(stale_key);
        }

        order_.push_front(key);
        cache_[key] = {*result, order_.begin()};
    }

private:
    size_t capacity_;
    std::list<uint64_t> order_;
    std::unordered_map<uint64_t, std::pair<AIRecognitionResult, std::list<uint64_t>::iterator>> cache_;
};

}  // namespace medical_display

#endif
