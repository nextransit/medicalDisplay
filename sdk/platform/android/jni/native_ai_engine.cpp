/**
 * @file native_ai_engine.cpp
 * @brief Android AI engine JNI bridge with heuristic fallback.
 */

#include <jni.h>
#include <android/log.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ai_engine.h"

#define LOG_TAG "MedicalDisplay_AI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

constexpr jint RAW_RESULT_SIZE = 14;

struct EngineContext {
    std::string model_path;
    bool prefer_npu;
    int num_threads;
    float score_threshold;
    uint64_t total_inferences;
    float avg_latency_ms;
};

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string jstring_to_string(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return "";
    }
    const char* raw = env->GetStringUTFChars(value, nullptr);
    if (raw == nullptr) {
        return "";
    }
    std::string result(raw);
    env->ReleaseStringUTFChars(value, raw);
    return result;
}

int modality_from_token(const std::string& token) {
    const std::string lower = to_lower(token);
    if (lower.empty()) {
        return MODALITY_UNKNOWN;
    }
    if (lower.find("ct") != std::string::npos) {
        return MODALITY_CT;
    }
    if (lower.find("mri") != std::string::npos || lower.find("mr") != std::string::npos) {
        return MODALITY_MR;
    }
    if (lower.find("dx") != std::string::npos || lower.find("dr") != std::string::npos) {
        return MODALITY_DX;
    }
    if (lower.find("cr") != std::string::npos) {
        return MODALITY_CR;
    }
    if (lower.find("us") != std::string::npos || lower.find("ultra") != std::string::npos) {
        return MODALITY_US;
    }
    if (lower.find("endo") != std::string::npos || lower.find("scope") != std::string::npos) {
        return MODALITY_ES;
    }
    if (lower.find("path") != std::string::npos
        || lower.find("slide") != std::string::npos
        || lower.find("wsi") != std::string::npos) {
        return MODALITY_SM;
    }
    if (lower.find("pet") != std::string::npos) {
        return MODALITY_PT;
    }
    if (lower.find("angi") != std::string::npos || lower.find("xa") != std::string::npos) {
        return MODALITY_XA;
    }
    if (lower.find("fluoro") != std::string::npos || lower.find("rf") != std::string::npos) {
        return MODALITY_RF;
    }
    if (lower.find("oph") != std::string::npos || lower.find("retina") != std::string::npos) {
        return MODALITY_OP;
    }
    if (lower.find("surg") != std::string::npos || lower.find("or ") != std::string::npos) {
        return MODALITY_SURGICAL;
    }
    return MODALITY_UNKNOWN;
}

int infer_modality_from_pixels(const jbyte* data, jsize size, int width, int height, int channels) {
    if (data == nullptr || size <= 0 || width <= 0 || height <= 0) {
        return MODALITY_UNKNOWN;
    }

    const size_t sample_step = static_cast<size_t>(std::max(1, size / 4096));
    double sum = 0.0;
    double sum_sq = 0.0;
    double saturation = 0.0;
    size_t samples = 0;

    for (size_t index = 0; index < static_cast<size_t>(size); index += sample_step) {
        const uint8_t value = static_cast<uint8_t>(data[index]);
        sum += value;
        sum_sq += static_cast<double>(value) * value;
        if (channels >= 3) {
            const size_t base = index - (index % static_cast<size_t>(channels));
            if (base + 2 < static_cast<size_t>(size)) {
                const uint8_t r = static_cast<uint8_t>(data[base]);
                const uint8_t g = static_cast<uint8_t>(data[base + 1]);
                const uint8_t b = static_cast<uint8_t>(data[base + 2]);
                saturation += std::max({r, g, b}) - std::min({r, g, b});
            }
        }
        ++samples;
    }

    if (samples == 0) {
        return MODALITY_UNKNOWN;
    }

    const double mean = sum / samples;
    const double variance = std::max(0.0, (sum_sq / samples) - (mean * mean));
    const double avg_saturation = channels >= 3 ? saturation / samples : 0.0;

    if (channels >= 3) {
        if (avg_saturation > 32.0) {
            return (width * height > 3840 * 2160) ? MODALITY_SM : MODALITY_ES;
        }
        if (avg_saturation > 18.0) {
            return MODALITY_US;
        }
        return MODALITY_SURGICAL;
    }

    if (variance > 4800.0 && mean < 120.0) {
        return MODALITY_CT;
    }
    if (variance > 2400.0) {
        return MODALITY_DX;
    }
    if (mean > 175.0 && variance < 900.0) {
        return MODALITY_US;
    }
    return MODALITY_MR;
}

void fill_strategy(int modality, float* raw) {
    raw[4] = 2.2f;
    raw[5] = 0.0f;
    raw[6] = 0.0f;
    raw[7] = 127.0f;
    raw[8] = 255.0f;
    raw[9] = 0.0f;
    raw[10] = 1.0f;
    raw[11] = 1.0f;
    raw[12] = 0.0f;
    raw[13] = 0.0f;

    switch (modality) {
        case MODALITY_CT:
            raw[4] = 2.0f;
            raw[5] = 5.0f;
            raw[6] = 1.0f;
            raw[7] = 40.0f;
            raw[8] = 400.0f;
            raw[9] = 1.0f;
            raw[10] = 1.2f;
            raw[11] = 1.1f;
            break;
        case MODALITY_MR:
            raw[5] = 5.0f;
            raw[6] = 1.0f;
            raw[7] = 500.0f;
            raw[8] = 1000.0f;
            raw[9] = 2.0f;
            break;
        case MODALITY_US:
            raw[9] = 3.0f;
            raw[11] = 1.15f;
            break;
        case MODALITY_ES:
        case MODALITY_SURGICAL:
            raw[4] = 2.4f;
            raw[5] = 1.0f;
            raw[9] = 5.0f;
            raw[12] = 1.0f;
            raw[13] = 2.0f;
            break;
        case MODALITY_SM:
            raw[9] = 4.0f;
            raw[10] = 1.3f;
            break;
        default:
            break;
    }
}

float confidence_from_modality(int modality, double variance, bool hinted, float threshold) {
    float confidence = hinted ? 0.93f : 0.68f;
    if (modality == MODALITY_UNKNOWN) {
        confidence = 0.40f;
    } else if (variance > 4500.0) {
        confidence += 0.08f;
    } else if (variance < 500.0) {
        confidence -= 0.06f;
    }
    return std::max(threshold, std::min(0.99f, confidence));
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_medicaldisplay_AIEngineWrapper_nativeCreate(
    JNIEnv* env,
    jclass clazz,
    jstring model_path,
    jboolean prefer_npu,
    jint num_threads,
    jfloat score_threshold
) {
    (void)env;
    (void)clazz;
    auto* context = new EngineContext();
    context->model_path = jstring_to_string(env, model_path);
    context->prefer_npu = prefer_npu == JNI_TRUE;
    context->num_threads = std::max(1, static_cast<int>(num_threads));
    context->score_threshold = std::max(0.1f, std::min(1.0f, score_threshold));
    context->total_inferences = 0;
    context->avg_latency_ms = 0.0f;
    LOGI("AI context created, model=%s, prefer_npu=%d, threads=%d",
         context->model_path.c_str(),
         context->prefer_npu ? 1 : 0,
         context->num_threads);
    return reinterpret_cast<jlong>(context);
}

JNIEXPORT void JNICALL
Java_com_medicaldisplay_AIEngineWrapper_nativeDestroy(JNIEnv* env, jclass clazz, jlong handle) {
    (void)env;
    (void)clazz;
    auto* context = reinterpret_cast<EngineContext*>(handle);
    delete context;
}

JNIEXPORT jfloatArray JNICALL
Java_com_medicaldisplay_AIEngineWrapper_nativeRecognize(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jbyteArray image_data,
    jint width,
    jint height,
    jint channels,
    jstring modality_hint,
    jstring series_description,
    jint body_part
) {
    (void)clazz;
    auto* context = reinterpret_cast<EngineContext*>(handle);
    if (context == nullptr || image_data == nullptr) {
        return nullptr;
    }

    auto started_at = std::chrono::steady_clock::now();
    std::string hint = jstring_to_string(env, modality_hint);
    std::string series = jstring_to_string(env, series_description);

    int modality = modality_from_token(hint);
    bool hinted = modality != MODALITY_UNKNOWN;
    if (!hinted) {
        modality = modality_from_token(series);
        hinted = modality != MODALITY_UNKNOWN;
    }

    const jsize size = env->GetArrayLength(image_data);
    jbyte* bytes = env->GetByteArrayElements(image_data, nullptr);
    if (bytes == nullptr) {
        return nullptr;
    }

    double sum = 0.0;
    double sum_sq = 0.0;
    if (!hinted) {
        modality = infer_modality_from_pixels(bytes, size, width, height, channels);
        const size_t sample_step = static_cast<size_t>(std::max<jsize>(1, size / 4096));
        size_t sample_count = 0;
        for (size_t index = 0; index < static_cast<size_t>(size); index += sample_step) {
            const double value = static_cast<uint8_t>(bytes[index]);
            sum += value;
            sum_sq += value * value;
            ++sample_count;
        }
        if (sample_count == 0) {
            sample_count = 1;
        }
        sum /= sample_count;
        sum_sq = std::max(0.0, (sum_sq / sample_count) - (sum * sum));
    }

    env->ReleaseByteArrayElements(image_data, bytes, JNI_ABORT);

    auto ended_at = std::chrono::steady_clock::now();
    const float inference_time_ms = std::chrono::duration<float, std::milli>(ended_at - started_at).count();

    context->total_inferences += 1;
    context->avg_latency_ms += (inference_time_ms - context->avg_latency_ms) / context->total_inferences;

    jfloat raw[RAW_RESULT_SIZE] = {};
    raw[0] = static_cast<jfloat>(modality);
    raw[1] = confidence_from_modality(modality, sum_sq, hinted, context->score_threshold);
    raw[2] = inference_time_ms;
    raw[3] = static_cast<jfloat>(body_part);
    fill_strategy(modality, raw);

    jfloatArray result = env->NewFloatArray(RAW_RESULT_SIZE);
    if (result == nullptr) {
        return nullptr;
    }
    env->SetFloatArrayRegion(result, 0, RAW_RESULT_SIZE, raw);
    return result;
}

JNIEXPORT jfloatArray JNICALL
Java_com_medicaldisplay_AIEngineWrapper_nativeGetWindowRecommendations(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jint modality
) {
    (void)clazz;
    (void)handle;
    std::vector<jfloat> values;
    switch (modality) {
        case MODALITY_CT:
            values = {40.0f, 400.0f, -600.0f, 1500.0f, 300.0f, 2000.0f};
            break;
        case MODALITY_MR:
            values = {500.0f, 1000.0f};
            break;
        case MODALITY_US:
            values = {127.0f, 255.0f};
            break;
        default:
            values = {127.0f, 255.0f};
            break;
    }

    jfloatArray result = env->NewFloatArray(static_cast<jsize>(values.size()));
    if (result == nullptr) {
        LOGW("Failed to allocate window recommendation array");
        return nullptr;
    }
    env->SetFloatArrayRegion(result, 0, static_cast<jsize>(values.size()), values.data());
    return result;
}

}  // extern "C"
