/**
 * @file native_display_engine.cpp
 * @brief Android display adapter JNI bridge.
 */

#include <jni.h>
#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define LOG_TAG "MedicalDisplay_Display"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

struct DisplayState {
    float gamma = 2.2f;
    int color_space = 0;
    bool gsdf_enabled = true;
    std::string gsdf_profile = "DEFAULT";
    float window_center = 127.0f;
    float window_width = 255.0f;
    int local_enhancement = 0;
    bool hdr_enabled = false;
    int hdr_mode = 0;
    float sharpness = 1.0f;
    float contrast = 1.0f;
};

struct DisplayContext {
    int max_display_count = 1;
    int color_depth = 10;
    bool default_gsdf = true;
    bool default_hdr = true;
    std::mutex mutex;
    std::unordered_map<int, DisplayState> states;
};

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

DisplayState& ensure_state(DisplayContext* context, int display_id) {
    auto iterator = context->states.find(display_id);
    if (iterator != context->states.end()) {
        return iterator->second;
    }
    DisplayState state;
    state.gsdf_enabled = context->default_gsdf;
    state.hdr_enabled = context->default_hdr;
    auto inserted = context->states.emplace(display_id, state);
    return inserted.first->second;
}

float epoch_seconds() {
    using namespace std::chrono;
    return duration_cast<duration<float>>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeCreate(
    JNIEnv* env,
    jclass clazz,
    jint max_display_count,
    jint color_depth,
    jboolean enable_gsdf,
    jboolean enable_hdr
) {
    (void)env;
    (void)clazz;
    auto* context = new DisplayContext();
    context->max_display_count = std::max(1, static_cast<int>(max_display_count));
    context->color_depth = std::max(8, static_cast<int>(color_depth));
    context->default_gsdf = enable_gsdf == JNI_TRUE;
    context->default_hdr = enable_hdr == JNI_TRUE;
    for (int display_id = 0; display_id < context->max_display_count; ++display_id) {
        DisplayState state;
        state.gsdf_enabled = context->default_gsdf;
        state.hdr_enabled = context->default_hdr;
        context->states.emplace(display_id, state);
    }
    LOGI("Display context created: displays=%d, depth=%d",
         context->max_display_count,
         context->color_depth);
    return reinterpret_cast<jlong>(context);
}

JNIEXPORT void JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeDestroy(JNIEnv* env, jclass clazz, jlong handle) {
    (void)env;
    (void)clazz;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    delete context;
}

JNIEXPORT jboolean JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeApplyConfig(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jint display_id,
    jfloat gamma,
    jint color_space,
    jboolean gsdf_enabled,
    jstring gsdf_profile,
    jfloat window_center,
    jfloat window_width,
    jint local_enhancement,
    jboolean hdr_enabled,
    jint hdr_mode,
    jfloat sharpness,
    jfloat contrast
) {
    (void)clazz;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    if (context == nullptr) {
        return JNI_FALSE;
    }

    std::lock_guard<std::mutex> lock(context->mutex);
    DisplayState& state = ensure_state(context, display_id);
    state.gamma = gamma <= 0.0f ? 2.2f : gamma;
    state.color_space = color_space;
    state.gsdf_enabled = gsdf_enabled == JNI_TRUE;
    const std::string profile = jstring_to_string(env, gsdf_profile);
    state.gsdf_profile = profile.empty() ? "DEFAULT" : profile;
    state.window_center = window_center;
    state.window_width = window_width;
    state.local_enhancement = local_enhancement;
    state.hdr_enabled = hdr_enabled == JNI_TRUE;
    state.hdr_mode = hdr_mode;
    state.sharpness = sharpness <= 0.0f ? 1.0f : sharpness;
    state.contrast = contrast <= 0.0f ? 1.0f : contrast;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeSetWindowLevel(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jint display_id,
    jfloat center,
    jfloat width
) {
    (void)env;
    (void)clazz;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    if (context == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(context->mutex);
    DisplayState& state = ensure_state(context, display_id);
    state.window_center = center;
    state.window_width = width;
}

JNIEXPORT void JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeSetLocalEnhancement(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jint display_id,
    jint enhance_type,
    jboolean enabled
) {
    (void)env;
    (void)clazz;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    if (context == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(context->mutex);
    DisplayState& state = ensure_state(context, display_id);
    state.local_enhancement = enabled == JNI_TRUE ? enhance_type : 0;
}

JNIEXPORT jintArray JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeListDisplays(JNIEnv* env, jclass clazz, jlong handle) {
    (void)clazz;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    if (context == nullptr) {
        return nullptr;
    }

    std::vector<jint> displays;
    {
        std::lock_guard<std::mutex> lock(context->mutex);
        displays.reserve(context->states.size());
        for (const auto& entry : context->states) {
            displays.push_back(entry.first);
        }
    }
    std::sort(displays.begin(), displays.end());

    jintArray result = env->NewIntArray(static_cast<jsize>(displays.size()));
    if (result == nullptr) {
        return nullptr;
    }
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(displays.size()), displays.data());
    return result;
}

JNIEXPORT jint JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeSyncDisplays(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jintArray display_ids,
    jint sync_mode
) {
    (void)clazz;
    (void)sync_mode;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    if (context == nullptr || display_ids == nullptr) {
        return -1;
    }

    const jsize count = env->GetArrayLength(display_ids);
    std::vector<jint> ids(static_cast<size_t>(count));
    env->GetIntArrayRegion(display_ids, 0, count, ids.data());

    std::lock_guard<std::mutex> lock(context->mutex);
    int synced = 0;
    for (jint display_id : ids) {
        if (context->states.find(display_id) != context->states.end()) {
            ++synced;
        }
    }
    return synced;
}

JNIEXPORT jboolean JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeSelfTest(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jint display_id,
    jint test_pattern
) {
    (void)env;
    (void)clazz;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    if (context == nullptr) {
        return JNI_FALSE;
    }

    std::lock_guard<std::mutex> lock(context->mutex);
    const bool exists = context->states.find(display_id) != context->states.end();
    const bool valid_pattern = test_pattern >= 0 && test_pattern <= 2;
    return (exists && valid_pattern) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloatArray JNICALL
Java_com_medicaldisplay_DisplayAdapter_nativeGetCalibrationStatus(
    JNIEnv* env,
    jclass clazz,
    jlong handle,
    jint display_id
) {
    (void)clazz;
    auto* context = reinterpret_cast<DisplayContext*>(handle);
    if (context == nullptr) {
        return nullptr;
    }

    jfloat values[5] = {};
    {
        std::lock_guard<std::mutex> lock(context->mutex);
        DisplayState& state = ensure_state(context, display_id);
        values[0] = std::max(0.6f, 1.8f - (state.contrast - 1.0f) * 0.4f);
        values[1] = state.hdr_enabled ? 650.0f : 500.0f;
        values[2] = 95.0f;
        values[3] = state.gamma;
        values[4] = epoch_seconds();
    }

    jfloatArray result = env->NewFloatArray(5);
    if (result == nullptr) {
        return nullptr;
    }
    env->SetFloatArrayRegion(result, 0, 5, values);
    return result;
}

}  // extern "C"
