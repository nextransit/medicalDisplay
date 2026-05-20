/**
 * @file onnx_backend.cpp
 * @brief ONNX Runtime backend implementation (ONNX Runtime 1.26+)
 */

#include "onnx_backend.h"
#include "ai_engine.h"

#include <cstring>
#include <stdexcept>

#ifdef AI_BACKEND_ONNXRUNTIME
#include <onnxruntime/onnxruntime_cxx_api.h>
#endif

namespace medical_display {

struct ONNXSession {
#ifdef AI_BACKEND_ONNXRUNTIME
    Ort::Env env;
    Ort::Session session;
    Ort::SessionOptions options;
    Ort::MemoryInfo memory_info;
    
    ONNXSession(Ort::Env&& e, Ort::Session&& s, Ort::SessionOptions&& o)
        : env(std::move(e)), session(std::move(s)), options(std::move(o)),
          memory_info(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault)) {}
#else
    int dummy = 0;
#endif
};

std::unique_ptr<ONNXBackend> ONNXBackend::Create(const char* model_path, bool use_gpu) {
    if (!model_path || model_path[0] == '\0') {
        return nullptr;
    }
    
    try {
#ifdef AI_BACKEND_ONNXRUNTIME
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MedicalDisplay");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        
        if (use_gpu) {
            // 尝试 CUDA
            try {
                OrtCUDAProviderOptions cuda_opts;
                options.AppendExecutionProvider_CUDA(cuda_opts);
            } catch (...) {
                // CUDA 不可用，继续用 CPU
            }
        }
        
        Ort::Session session(env, model_path, options);
        auto session_ptr = std::make_unique<ONNXSession>(
            std::move(env), std::move(session), std::move(options));
        
        auto backend = std::unique_ptr<ONNXBackend>(new ONNXBackend(std::move(session_ptr)));
        
        // 获取输入输出维度
        Ort::AllocatorWithDefaultOptions allocator;
        size_t num_input_nodes = backend->session_->session.GetInputCount();
        size_t num_output_nodes = backend->session_->session.GetOutputCount();
        
        if (num_input_nodes > 0) {
            auto type_info = backend->session_->session.GetInputTypeInfo(0);
            auto input_shape = type_info.GetTensorTypeAndShapeInfo().GetShape();
            backend->input_shape_ = input_shape;
            auto in_name = backend->session_->session.GetInputNameAllocated(0, allocator);
            backend->input_node_name_ = in_name.get();
        }
        
        if (num_output_nodes > 0) {
            auto type_info = backend->session_->session.GetOutputTypeInfo(0);
            auto output_shape = type_info.GetTensorTypeAndShapeInfo().GetShape();
            backend->output_shape_ = output_shape;
            auto out_name = backend->session_->session.GetOutputNameAllocated(0, allocator);
            backend->output_node_name_ = out_name.get();
        }
        
        return backend;
#else
        (void)model_path;
        (void)use_gpu;
        return nullptr;
#endif
    } catch (const std::exception& e) {
        return nullptr;
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
        auto& memory_info = session_->memory_info;
        
        if (input_node_name_.empty() || output_node_name_.empty()) {
            return -1;
        }
        
        const char* input_names[] = { input_node_name_.c_str() };
        const char* output_names[] = { output_node_name_.c_str() };
        
        // 准备输入张量
        std::vector<int64_t> input_shape = input_shape_;
        if (input_shape.empty()) {
            input_shape = {1, input_size};
        }
        
        // 避免 std::vector<float> 的堆分配与数据拷贝，直接包装外部 input 缓冲
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, const_cast<float*>(input), static_cast<size_t>(input_size),
            input_shape.data(), input_shape.size());
        
        // 执行推理 (ONNX Runtime 1.26 返回 std::vector<Ort::Value>)
        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1);
        
        if (output_tensors.empty()) {
            return -1;
        }
        
        // 提取输出 (使用 template 关键字消歧义)
        auto& out_value = output_tensors.front();
        float* output_data = out_value.template GetTensorMutableData<float>();
        auto out_info = out_value.GetTensorTypeAndShapeInfo();
        size_t output_len = out_info.GetElementCount();
        
        size_t copy_size = std::min(static_cast<size_t>(output_size), output_len);
        std::memcpy(output, output_data, copy_size * sizeof(float));
        
        return 0;
    } catch (const std::exception& e) {
        return -1;
    } catch (...) {
        return -1;
    }
#else
    (void)input;
    (void)input_size;
    (void)output;
    (void)output_size;
    return -1;
#endif
}

}  // namespace medical_display
