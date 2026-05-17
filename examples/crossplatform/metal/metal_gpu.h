/**
 * Metal GPU Device - macOS 实现
 */
#ifndef METAL_GPU_H
#define METAL_GPU_H

#import <Metal/Metal.h>
#include <memory>
#include "gpu_compute.h"

namespace md {

class MetalGPUDevice : public IGPUDevice {
public:
    MetalGPUDevice();
    ~MetalGPUDevice() override;
    
    std::string getName() const override;
    GPUBackend getBackend() const override { return GPUBackend::Metal; }
    bool isSupported() const override;
    
    bool initialize() override;
    void* getInternalDevice() override { return (__bridge void*)device_; }
    
    bool createTextures(int width, int height) override;
    void processFrame() override;
    void generateTestPattern(int mode) override;
    void waitIdle() override;
    
    id<MTLDevice> getDevice() { return device_; }
    id<MTLCommandQueue> getQueue() { return commandQueue_; }
    id<MTLComputePipelineState> getPipeline() { return processPipeline_; }
    id<MTLComputePipelineState> getGenPipeline() { return generatePipeline_; }
    id<MTLTexture> getInputTexture() { return inputTexture_; }
    id<MTLTexture> getOutputTexture() { return outputTexture_; }
    
private:
    bool compileShaders();
    
    id<MTLDevice> device_;
    id<MTLCommandQueue> commandQueue_;
    id<MTLLibrary> library_;
    id<MTLComputePipelineState> processPipeline_;
    id<MTLComputePipelineState> generatePipeline_;
    id<MTLTexture> inputTexture_;
    id<MTLTexture> outputTexture_;
    int textureWidth_;
    int textureHeight_;
};

}  // namespace md

#endif  // METAL_GPU_H
