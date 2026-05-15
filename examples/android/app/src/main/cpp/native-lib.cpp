#include <jni.h>

#include <mutex>
#include <string>

namespace {

std::mutex g_mutex;
std::string g_last_study = "No synthetic study generated yet";

jstring toJString(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_medicaldisplay_examples_NativeBridge_nativeStatus(JNIEnv* env, jobject) {
    return toJString(env, "Native demo ready - Vulkan/AI hooks can be attached here");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_medicaldisplay_examples_NativeBridge_dicomReceiverStatus(JNIEnv* env, jobject) {
    return toJString(env, "DICOM C-STORE demo endpoint: configurable in native layer");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_medicaldisplay_examples_NativeBridge_multiDisplayStatus(JNIEnv* env, jobject) {
    return toJString(env, "Multi-display sync demo: simulated 60 FPS lock-step pipeline");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_medicaldisplay_examples_NativeBridge_cloudDemoStatus(JNIEnv* env, jobject) {
    return toJString(env, "Cloud demo: OTA manifest polling and staged rollout simulation");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_medicaldisplay_examples_NativeBridge_lastSyntheticStudy(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return toJString(env, g_last_study);
}

extern "C" JNIEXPORT void JNICALL
Java_com_medicaldisplay_examples_NativeBridge_generateSyntheticStudy(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_study = "Synthetic CT study generated; ready for render / C-STORE / OTA flow";
}
