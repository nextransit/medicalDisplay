/**
 * @file onnx_backend.h
 * @brief ONNX Runtime backend for AI inference
 */

#ifndef MEDICALDISPLAY_ONNX_BACKEND_H
#define MEDICALDISPLAY_ONNX_BACKEND_H

#include "ai_engine.h"

#include <memory>
#include <string>
#include <vector>

namespace medical_display {

// Forward declarations
struct ONNXSession;

class ONNXBackend {
public:
    /**
     * 创建 ONNX Runtime 后端
     * @param model_path 模型文件路径
     * @param use_gpu 是否使用 GPU
     * @return nullptr 失败，有效指针成功
     */
    static std::unique_ptr<ONNXBackend> Create(const char* model_path, bool use_gpu);
    
    ~ONNXBackend();
    
    /**
     * 执行推理
     * @param input 输入张量
     * @param input_size 输入大小
     * @param output 输出张量
     * @param output_size 输出大小
     * @return 0 成功
     */
    int Run(const float* input, int input_size, float* output, int output_size);
    
    /**
     * 获取输入维度
     */
    const std::vector<int64_t>& GetInputShape() const { return input_shape_; }
    
    /**
     * 获取输出维度
     */
    const std::vector<int64_t>& GetOutputShape() const { return output_shape_; }
    
    bool IsValid() const { return session_ != nullptr; }

private:
    explicit ONNXBackend(std::unique_ptr<ONNXSession> session);
    
    std::unique_ptr<ONNXSession> session_;
    std::vector<int64_t> input_shape_;
    std::vector<int64_t> output_shape_;
};

}  // namespace medical_display

#endif  // MEDICALDISPLAY_ONNX_BACKEND_H
