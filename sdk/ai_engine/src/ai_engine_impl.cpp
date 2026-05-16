#include "ai_engine.h"
#include "modality_strategy.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cassert>
#include <memory>

namespace medical_display {
class ModelRunner;
class MetadataCache;
}

struct AIEngine {
    AIEngineConfig config{};
    std::unique_ptr<medical_display::ModelRunner> model_runner;
    std::unique_ptr<medical_display::MetadataCache> metadata_cache;
    uint64_t total_inferences = 0;
    float cumulative_latency_ms = 0.0f;
};

namespace medical_display {

static const DisplayStrategy DEFAULT_STRATEGIES[MODALITY_COUNT] = {
    {2.2f, 0, 1, 127.0f, 256.0f, 0, 1.0f, 1.0f, false, 0},
    {2.0f, 3, 1, 40.0f, 400.0f, 1, 1.2f, 1.1f, false, 0},
    {2.2f, 3, 1, 500.0f, 1000.0f, 2, 1.1f, 1.0f, false, 0},
    {2.2f, 3, 1, 2000.0f, 4000.0f, 1, 1.5f, 1.2f, false, 0},
    {2.2f, 3, 1, 2000.0f, 4000.0f, 1, 1.5f, 1.2f, false, 0},
    {2.2f, 0, 0, 127.0f, 255.0f, 3, 1.0f, 1.15f, false, 0},
    {2.4f, 1, 0, 127.0f, 255.0f, 5, 1.1f, 1.1f, true, 1},
    {2.2f, 0, 0, 127.0f, 255.0f, 4, 1.3f, 1.1f, false, 0},
    {2.2f, 1, 0, 0.0f, 10.0f, 0, 1.2f, 1.2f, true, 2},
    {2.2f, 0, 0, 0.0f, 255.0f, 3, 1.1f, 1.1f, true, 1},
    {2.2f, 0, 0, 0.0f, 255.0f, 0, 1.0f, 1.0f, false, 0},
    {2.2f, 0, 0, 127.0f, 255.0f, 0, 1.2f, 1.0f, false, 0},
    {2.4f, 1, 0, 127.0f, 255.0f, 5, 1.0f, 1.05f, true, 2},
};

static ModalityType parse_modality_tag(const char* tag) {
    if (!tag) return MODALITY_UNKNOWN;
    if (std::strcmp(tag, "CT") == 0) return MODALITY_CT;
    if (std::strcmp(tag, "MR") == 0) return MODALITY_MR;
    if (std::strcmp(tag, "DX") == 0) return MODALITY_DX;
    if (std::strcmp(tag, "CR") == 0) return MODALITY_CR;
    if (std::strcmp(tag, "US") == 0) return MODALITY_US;
    if (std::strcmp(tag, "ES") == 0) return MODALITY_ES;
    if (std::strcmp(tag, "SM") == 0) return MODALITY_SM;
    if (std::strcmp(tag, "PT") == 0) return MODALITY_PT;
    if (std::strcmp(tag, "XA") == 0) return MODALITY_XA;
    if (std::strcmp(tag, "RF") == 0) return MODALITY_RF;
    if (std::strcmp(tag, "OP") == 0) return MODALITY_OP;
    if (std::strstr(tag, "CT Image") != nullptr) return MODALITY_CT;
    if (std::strstr(tag, "MR Image") != nullptr) return MODALITY_MR;
    if (std::strstr(tag, "Ultrasound") != nullptr) return MODALITY_US;
    return MODALITY_UNKNOWN;
}

static void update_stats(::AIEngine* engine, float latency_ms) {
    engine->total_inferences++;
    engine->cumulative_latency_ms += latency_ms;
}

static void fill_best_result(const float* scores, AIRecognitionResult* result) {
    int best_idx = 0;
    float best_score = scores[0];
    for (int i = 1; i < MODALITY_COUNT; ++i) {
        if (scores[i] > best_score) {
            best_score = scores[i];
            best_idx = i;
        }
    }

    // [FIX #6] 双重保护：assert 开发期捕获 + 运行时 fallback
    assert(best_idx >= 0 && best_idx < MODALITY_COUNT);
    if (best_idx < 0 || best_idx >= MODALITY_COUNT) {
        best_idx = MODALITY_UNKNOWN;
        best_score = 0.0f;
    }

    result->modality = static_cast<ModalityType>(best_idx);
    result->confidence = best_score;
    result->strategy = DEFAULT_STRATEGIES[best_idx];
}

float* preprocess_dicom(const uint16_t* data, int w, int h, int bits, int target_w, int target_h) {
    // [P1-OPT] Fused preprocessing: allocate + resize + normalize in single pass
    float* output = new float[static_cast<size_t>(target_w) * target_h];

    const float scale = 1.0f / ((1 << bits) - 1);
    const float inv_stddev = 1.0f / 0.226f;  // Precompute inverse for multiply
    const float mean = 0.449f;
    const float x_ratio = static_cast<float>(w) / target_w;
    const float y_ratio = static_cast<float>(h) / target_h;

    // [P1-OPT] Fused loop: resize + normalize in single pass (eliminates separate normalize pass)
    for (int y = 0; y < target_h; ++y) {
        for (int x = 0; x < target_w; ++x) {
            int src_x = std::min(static_cast<int>(x * x_ratio), w - 1);
            int src_y = std::min(static_cast<int>(y * y_ratio), h - 1);
            float val = data[src_y * w + src_x] * scale;
            output[y * target_w + x] = (val - mean) * inv_stddev;
        }
    }

    return output;
}

float* preprocess_image(const uint8_t* data, int w, int h, int channels, int target_w, int target_h) {
    // [P1-OPT] Fused preprocessing: allocate + resize + normalize in single pass
    float* output = new float[static_cast<size_t>(target_w) * target_h];

    const float inv_stddev = 1.0f / 0.226f;  // Precompute inverse for multiply
    const float mean = 0.449f;
    const float x_ratio = static_cast<float>(w) / target_w;
    const float y_ratio = static_cast<float>(h) / target_h;

    // [P1-OPT] Fused loop: resize + grayscale conversion + normalize in single pass
    for (int y = 0; y < target_h; ++y) {
        for (int x = 0; x < target_w; ++x) {
            int src_x = std::min(static_cast<int>(x * x_ratio), w - 1);
            int src_y = std::min(static_cast<int>(y * y_ratio), h - 1);
            const uint8_t* pixel = &data[(src_y * w + src_x) * channels];
            float val = (pixel[0] * 0.299f + pixel[1] * 0.587f + pixel[2] * 0.114f) / 255.0f;
            output[y * target_w + x] = (val - mean) * inv_stddev;
        }
    }

    return output;
}

// [P2-OPT] Optimized normalize_tensor - precompute inverse and use multiply instead of divide
void normalize_tensor(float* data, int size, float mean, float stddev) {
    // Precompute inverse to replace expensive division with multiplication
    const float inv_stddev = 1.0f / stddev;
    for (int i = 0; i < size; ++i) {
        data[i] = (data[i] - mean) * inv_stddev;
    }
}

uint64_t hash_metadata_key(const char* modality, const char* series, int body_part) {
    uint64_t hash = 1469598103934665603ULL;
    if (modality) {
        for (const char* p = modality; *p; ++p) {
            hash ^= static_cast<uint8_t>(*p);
            hash *= 1099511628211ULL;
        }
    }
    if (series) {
        for (const char* p = series; *p; ++p) {
            hash ^= static_cast<uint8_t>(*p);
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<uint64_t>(body_part);
    hash *= 1099511628211ULL;
    return hash;
}

}  // namespace medical_display

extern "C" {

AIEngine* ai_engine_create(const AIEngineConfig* config) {
    if (!config) return nullptr;

    auto* engine = new AIEngine{};
    engine->config = *config;
    engine->model_runner = medical_display::ModelRunner::Create(engine->config);
    engine->metadata_cache = std::make_unique<medical_display::MetadataCache>(256);
    return engine;
}

void ai_engine_destroy(AIEngine* engine) {
    delete engine;
}

int ai_engine_reload_model(AIEngine* engine, const char* model_path) {
    if (!engine || !model_path) return -1;
    std::strncpy(engine->config.model_path, model_path, sizeof(engine->config.model_path) - 1);
    engine->config.model_path[sizeof(engine->config.model_path) - 1] = '\0';
    engine->model_runner = medical_display::ModelRunner::Create(engine->config);
    return 0;
}

int ai_engine_recognize_from_dicom(AIEngine* engine, const uint16_t* dicom_data, int width, int height, int bits_allocated, AIRecognitionResult* result) {
    if (!engine || !dicom_data || !result) return -1;

    auto start = std::chrono::high_resolution_clock::now();
    std::unique_ptr<float[]> input(medical_display::preprocess_dicom(
        dicom_data, width, height, bits_allocated, engine->config.input_width, engine->config.input_height));

    float scores[MODALITY_COUNT];
    if (engine->model_runner->Run(input.get(), scores, MODALITY_COUNT) != 0) {
        return -1;
    }

    medical_display::fill_best_result(scores, result);
    auto end = std::chrono::high_resolution_clock::now();
    result->inference_time_ms = std::chrono::duration<float, std::milli>(end - start).count();
    medical_display::update_stats(engine, result->inference_time_ms);
    return 0;
}

int ai_engine_recognize_from_image(AIEngine* engine, const uint8_t* image_data, int width, int height, int channels, AIRecognitionResult* result) {
    if (!engine || !image_data || !result) return -1;

    auto start = std::chrono::high_resolution_clock::now();
    std::unique_ptr<float[]> input(medical_display::preprocess_image(
        image_data, width, height, channels, engine->config.input_width, engine->config.input_height));

    float scores[MODALITY_COUNT];
    if (engine->model_runner->Run(input.get(), scores, MODALITY_COUNT) != 0) {
        return -1;
    }

    medical_display::fill_best_result(scores, result);
    auto end = std::chrono::high_resolution_clock::now();
    result->inference_time_ms = std::chrono::duration<float, std::milli>(end - start).count();
    medical_display::update_stats(engine, result->inference_time_ms);
    return 0;
}

int ai_engine_recognize_from_metadata(AIEngine* engine, const char* modality_tag, const char* series_desc, int body_part, AIRecognitionResult* result) {
    if (!engine || !result) return -1;

    uint64_t cache_key = medical_display::hash_metadata_key(modality_tag, series_desc, body_part);
    if (engine->metadata_cache->Get(cache_key, result)) {
        result->inference_time_ms = 0.1f;
        return 0;
    }

    result->modality = medical_display::parse_modality_tag(modality_tag);
    result->confidence = 0.95f;
    result->strategy = medical_display::DEFAULT_STRATEGIES[result->modality];
    result->body_part = body_part;
    result->inference_time_ms = 0.5f;

    engine->metadata_cache->Put(cache_key, result);
    medical_display::update_stats(engine, result->inference_time_ms);
    return 0;
}

int ai_engine_recognize_batch(AIEngine* engine, const uint8_t** frames, int frame_count, AIRecognitionResult* results) {
    if (!engine || !frames || !results || frame_count <= 0) return -1;

    int processed = 0;
    for (int index = 0; index < frame_count; ++index) {
        if (ai_engine_recognize_from_image(
                engine, frames[index], engine->config.input_width, engine->config.input_height, 3, &results[index]) == 0) {
            processed++;
        }
    }
    return processed;
}

void ai_engine_get_stats(AIEngine* engine, uint64_t* total_inferences, float* avg_latency_ms) {
    if (!engine) return;
    if (total_inferences) *total_inferences = engine->total_inferences;
    if (avg_latency_ms) {
        *avg_latency_ms = engine->total_inferences > 0
            ? engine->cumulative_latency_ms / static_cast<float>(engine->total_inferences)
            : 0.0f;
    }
}

void ai_engine_reset_stats(AIEngine* engine) {
    if (!engine) return;
    engine->total_inferences = 0;
    engine->cumulative_latency_ms = 0.0f;
}

}  // extern "C"
