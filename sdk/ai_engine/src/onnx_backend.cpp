/**
 * @file onnx_backend.cpp
 * @brief ONNX Runtime backend implementation
 */

#include "onnx_backend.h"
#include "ai_engine.h"

#include <cstring>
#include <stdexcept>

#ifdef AI_BACKEND_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace medical_display {

struct ONNXSession {
#ifdef AI_BACKEND_ONNXRUNTIME
    Ort::Env env;
    Ort::Session session;
    Ort::SessionOptions options;
    std::unique_ptr<Ort::MemoryInfo> memory_info;
    
    ONNXSession(Ort::Env&& e, Ort::Session&& s, Ort::SessionOptions&& o)
        : env(std::move(e)), session(std::move(s)), options(std::move(o)) {
        memory_info = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    }
#else
    // 空实现，无 ONNX Runtime
    int dummy = 0;
#endif
};

std::unique_ptr<ONNXBackend> ONNXBackend::Create(const char* model_path, bool use_gpu) {
    if (!model_path || model_path[0] == '\0') {
        return nullptr;
    }
    
    try {
#ifdef AI_BACKEND_ONNXRUNTIME
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING);
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        
        if (use_gpu) {
            // 尝试 CUDA
            std::vector<std::string> providers = {"CUDAExecutionProvider", "CPUExecutionProvider"};
            options.AppendExecutionProviders(providers);
        }
        
        Ort::Session session(env, model_path, options);
        auto session_ptr = std::make_unique<ONNXSession>(
            std::move(env), std::move(session), std::move(options));
        
        auto backend = std::unique_ptr<ONNXBackend>(new ONNXBackend(std::move(session_ptr)));
        
        // 获取输入输出维度
        size_t num_input_nodes = backend->session_->session.GetInputCount();
        size_t num_output_nodes = backend->session_->session.GetOutputCount();
        
        if (num_input_nodes > 0) {
            auto input_name_ptr = backend->session_->session.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
            auto type_info = backend->session_->session.GetInputTypeInfo(0);
            auto input_shape = type_info.GetTensorTypeAndShapeInfo().GetShape();
            backend->input_shape_ = input_shape;
        }
        
        if (num_output_nodes > 0) {
            auto type_info = backend->session_->session.GetOutputTypeInfo(0);
            auto output_shape = type_info.GetTensorTypeAndShapeInfo().GetShape();
            backend->output_shape_ = output_shape;
        }
        
        return backend;
#else
        (void)model_path;
        (void)use_gpu;
        return nullptr;  // 没有 ONNX Runtime
#endif
    } catch (...) {
        return nullptr;
    }
}

ONNXBackend::ONNXBackend(std::unique_ptr<ONNXSession> session)
    : session_(std::move(session)) {}

ONNXBackend::~ONNXBackend() = default;

int ONNXBackend::Run(const float* input, int input_size, float* output, int output_size) {
    if (!session_ || !input || !output) {
        return -1;
    }
    
#ifdef AI_BACKEND_ONNXRUNTIME
    try {
        auto& session = session_->session;
        auto& memory_info = *session_->memory_info;
        
        size_t num_input_nodes = session.GetInputCount();
        size_t num_output_nodes = session.GetOutputCount();
        
        if (num_input_nodes == 0 || num_output_nodes == 0) {
            return -1;
        }
        
        auto input_name = session.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
        auto output_name = session.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
        
        std::vector<const char*> input_names = {input_name.get()};
        std::vector<const char*> output_names = {output_name.get()};
        
        // 准备输入
        std::vector<int64_t> input_shape = input_shape_;
        if (input_shape.empty()) {
            input_shape = {1, input_size};
        }
        
        std::vector<float> input_tensor(input, input + input_size);
        
        // 执行推理
        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names.data(), &input_tensor, 1,
            output_names.data(), 1);
        
        // 提取输出
        float* output_data = output_tensors.GetOutput().GetTensorMutableData<float>();
        size_t output_len = output_tensors.GetOutput().GetTensorTypeAndShapeInfo().GetElementCount();
        
        size_t copy_size = std::min(static_cast<size_t>(output_size), output_len);
        std::memcpy(output, output_data, copy_size * sizeof(float));
        
        return 0;
    } catch (...) {
        return -1;
    }
#else
    (void)input;
    (void)input_size;
    (void)output;
    (void)output_size;
    return -1;  // 没有 ONNX Runtime 支持
#endif
}

}  // namespace medical_display
