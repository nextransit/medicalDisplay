/**
 * @file ai_engine_android.cpp
 * @brief AI Engine Android NPU Implementation
 */

#include "ai_engine.h"
#include <android/log.h>
#include <dlfcn.h>

#ifdef AI_NPU_RKNN

// RKNN headers
#include "rknn_api.h"

// RKNN function pointers
typedef rknn_context (*rknn_init_fn)(rknn_context*, const char*, rknn_init_cfg*);
typedef rknn_context (*rknn_destroy_fn)(rknn_context);
typedef rknn_context (*rknn_run_fn)(rknn_context, rknn_input*, rknn_output*, rknn_cmd(cmd));

static rknn_init_fn g_rknn_init = nullptr;
static rknn_destroy_fn g_rknn_destroy = nullptr;
static rknn_run_fn g_rknn_run = nullptr;
static void* g_rknn_lib = nullptr;

static bool load_rknn_library() {
    if (g_rknn_lib) return true;
    
    // Try to load RKNN library
    const char* paths[] = {
        "librockchip_npu.so",
        "librknn_runtime.so",
        "/system/lib64/librknn_runtime.so",
        nullptr
    };
    
    for (int i = 0; paths[i]; i++) {
        g_rknn_lib = dlopen(paths[i], RTLD_LAZY);
        if (g_rknn_lib) break;
    }
    
    if (!g_rknn_lib) {
        __android_log_print(ANDROID_LOG_ERROR, "AIEngine", "Failed to load RKNN library");
        return false;
    }
    
    g_rknn_init = (rknn_init_fn)dlsym(g_rknn_lib, "rknn_init");
    g_rknn_destroy = (rknn_destroy_fn)dlsym(g_rknn_lib, "rknn_destroy");
    g_rknn_run = (rknn_run_fn)dlsym(g_rknn_lib, "rknn_run");
    
    if (!g_rknn_init || !g_rknn_destroy || !g_rknn_run) {
        __android_log_print(ANDROID_LOG_ERROR, "AIEngine", "Failed to load RKNN functions");
        dlclose(g_rknn_lib);
        g_rknn_lib = nullptr;
        return false;
    }
    
    return true;
}

int ai_engine_load_rknn_model(void* ctx, const char* model_path) {
    if (!ctx || !model_path) return -1;
    
    auto* engine = static_cast<AI_EngineContext*>(ctx);
    
    if (!load_rknn_library()) {
        return -1;
    }
    
    rknn_context rknn_ctx;
    rknn_init_cfg cfg = {};
    cfg.npu_core_mask = 1 << engine->config.npu_core_id;
    
    int ret = g_rknn_init(&rknn_ctx, model_path, 0, &cfg);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "AIEngine", 
                           "rknn_init failed: %d", ret);
        return -1;
    }
    
    // Get model input/output info
    rknn_input_output_num io_num;
    g_rknn_get_input_output_num(rknn_ctx, &io_num);
    
    // Store context
    engine->model_handle = (void*)rknn_ctx;
    
    __android_log_print(ANDROID_LOG_INFO, "AIEngine", 
                       "RKNN model loaded: inputs=%d, outputs=%d",
                       io_num.n_input, io_num.n_output);
    
    return 0;
}

int ai_engine_run_rknn(void* ctx, const float* input, float* output, int batch) {
    if (!ctx || !input || !output) return -1;
    
    auto* engine = static_cast<AI_EngineContext*>(ctx);
    rknn_context rknn_ctx = (rknn_context)engine->model_handle;
    
    // Prepare input
    rknn_input inputs[1];
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_FLOAT32;
    inputs[0].fmt = RKNN_TENSOR_NCHW;
    inputs[0].size = batch * 3 * 224 * 224 * sizeof(float);
    inputs[0].buf = input;
    
    // Run inference
    int ret = g_rknn_run(rknn_ctx, inputs, 1, nullptr);
    if (ret < 0) {
        return -1;
    }
    
    // Get output
    rknn_output outputs[1];
    outputs[0].want_float = 1;
    
    g_rknn_outputs(rknn_ctx, outputs, 1);
    
    // Copy output
    memcpy(output, outputs[0].buf, outputs[0].size);
    
    // Release output
    g_rknn_release_outputs(rknn_ctx, outputs, 1);
    
    return 0;
}

void ai_engine_destroy_rknn(void* ctx) {
    if (!ctx) return;
    
    auto* engine = static_cast<AI_EngineContext*>(ctx);
    if (engine->model_handle && g_rknn_destroy) {
        g_rknn_destroy((rknn_context)engine->model_handle);
        engine->model_handle = nullptr;
    }
}

#else

int ai_engine_load_rknn_model(void* ctx, const char* model_path) {
    (void)ctx;
    (void)model_path;
    return -1;
}

int ai_engine_run_rknn(void* ctx, const float* input, float* output, int batch) {
    (void)ctx;
    (void)input;
    (void)output;
    (void)batch;
    return -1;
}

void ai_engine_destroy_rknn(void* ctx) {
    (void)ctx;
}

#endif // AI_NPU_RKNN
