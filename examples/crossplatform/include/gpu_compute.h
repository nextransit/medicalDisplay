/**
 * GPU Compute API - 跨平台 GPU 计算接口
 */
#ifndef GPU_COMPUTE_H
#define GPU_COMPUTE_H

#include <cstdint>
#include <string>
#include <vector>

namespace md {

// GPU 后端类型
enum class GPUBackend {
    Auto,
    Metal,       // macOS Apple Silicon/Intel
    Vulkan,      // Linux NVIDIA/AMD/Intel
    DirectX12,   // Windows
    CPU          // Fallback
};

// 流水线参数
struct PipelineParams {
    uint32_t width;
    uint32_t height;
    float brightness;
    float contrast;
    float saturation;
    int enableGsdf;
    int enableBloodless;
    float bloodSuppress;
    float tissueEnhance;
    float zoom;
    float panX;
    float panY;
    int mode;
};

// GPU 设备接口
class IGPUDevice {
public:
    virtual ~IGPUDevice() = default;
    
    virtual std::string getName() const = 0;
    virtual GPUBackend getBackend() const = 0;
    virtual bool isSupported() const = 0;
    
    virtual bool initialize() = 0;
    virtual void* getInternalDevice() = 0;
    
    virtual bool createTextures(int width, int height) = 0;
    virtual void processFrame() = 0;
    virtual void generateTestPattern(int mode) = 0;
    virtual void waitIdle() = 0;
};

// 工厂函数
std::unique_ptr<IGPUDevice> createGPUDevice(GPUBackend backend = GPUBackend::Auto);
std::vector<GPUBackend> enumerateAvailableBackends();

}  // namespace md

#endif  // GPU_COMPUTE_H
