/**
 * @file metal_compute.mm
 * @brief Metal Compute Shader 实现 - macOS/iOS GPU加速
 * 
 * 性能目标: 1920x1080 @ <5ms/frame (M1/M2)
 * 支持: 亮度/对比度、饱和度、GSDF、无血术野、Sobel边缘检测
 */

#if defined(__APPLE__)

#include "gpu_pipeline.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <Metal/Metal.h>
#include <MetalKit/MetalKit.h>

// ============================================================================
// Metal Compute Shader 源代码
// ============================================================================

static const char* MAIN_COMPUTE_SHADER = R"(
#include <metal_stdlib>
using namespace metal;

struct PipelineParams {
    uint width;
    uint height;
    float brightness;
    float contrast;
    float saturation;
    uint enable_gsdf;
    uint enable_bloodless;
    float blood_suppress;
    float tissue_enhance;
    uint enable_sobel;
    float sobel_threshold;
    uint enable_hdr;
    uint mode;
};

// RGB to HSV
float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB
float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// RGB to Grayscale
float rgb2gray(float3 c) {
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

// 主处理核
kernel void process_frame(texture2d<float, access::read> input [[texture(0)]],
                          texture2d<float, access::write> output [[texture(1)]],
                          texture1d<float, access::read> gsdfLut [[texture(2)]],
                          constant PipelineParams& params [[buffer(0)]],
                          uint2 gid [[thread_position_in_grid]]) {
    
    if (gid.x >= params.width || gid.y >= params.height) {
        return;
    }
    
    // 读取像素
    float4 pixel = input.read(gid);
    float3 rgb = pixel.rgb;
    
    // 1. 亮度/对比度
    if (params.brightness != 0.0 || params.contrast != 1.0) {
        rgb = (rgb - 0.5) * params.contrast + 0.5 + params.brightness;
        rgb = clamp(rgb, 0.0, 1.0);
    }
    
    // 2. 饱和度
    if (params.saturation != 1.0) {
        float3 hsv = rgb2hsv(rgb);
        hsv.y *= params.saturation;
        rgb = hsv2rgb(hsv);
    }
    
    // 3. GSDF查表
    if (params.enable_gsdf == 1) {
        float gray = rgb2gray(rgb);
        float gsdf_val = gsdfLut.read(uint(gray));
        rgb *= (gsdf_val / max(gray, 0.001));
    }
    
    // 4. 无血术野增强
    if (params.enable_bloodless == 1) {
        // 血色检测
        float blood_score = rgb.r * 0.5 - rgb.g * 0.3 - rgb.b * 0.2;
        float blood_mask = clamp(blood_score, 0.0, 1.0);
        
        // 抑制血色
        rgb.r *= 1.0 - blood_mask * params.blood_suppress * 0.3;
        rgb.g *= 1.0 + blood_mask * params.blood_suppress * 0.2;
        rgb.b *= 1.0 + blood_mask * params.blood_suppress * 0.2;
        
        // 组织对比度增强
        float gray = rgb2gray(rgb);
        rgb = mix(float3(gray), rgb, 1.0 + params.tissue_enhance * 0.2);
    }
    
    // 写入输出
    output.write(float4(clamp(rgb, 0.0, 1.0), 1.0), gid);
}

// Sobel边缘检测
kernel void sobel_edge(texture2d<float, access::read> input [[texture(0)]],
                       texture2d<float, access::write> output [[texture(1)]],
                       constant PipelineParams& params [[buffer(0)]],
                       uint2 gid [[thread_position_in_grid]]) {
    
    if (gid.x < 1 || gid.x >= params.width - 1 ||
        gid.y < 1 || gid.y >= params.height - 1) {
        output.write(float4(0.0), gid);
        return;
    }
    
    // 3x3邻域
    float tl = rgb2gray(input.read(gid + int2(-1,-1)).rgb);
    float tm = rgb2gray(input.read(gid + int2( 0,-1)).rgb);
    float tr = rgb2gray(input.read(gid + int2( 1,-1)).rgb);
    float ml = rgb2gray(input.read(gid + int2(-1, 0)).rgb);
    float mr = rgb2gray(input.read(gid + int2( 1, 0)).rgb);
    float bl = rgb2gray(input.read(gid + int2(-1, 1)).rgb);
    float bm = rgb2gray(input.read(gid + int2( 0, 1)).rgb);
    float br = rgb2gray(input.read(gid + int2( 1, 1)).rgb);
    
    // Sobel X
    float gx = -tl - 2*ml - bl + tr + 2*mr + br;
    
    // Sobel Y
    float gy = -tl - 2*tm - tr + bl + 2*bm + br;
    
    // 梯度幅度
    float mag = sqrt(gx*gx + gy*gy);
    float edge = (mag > params.sobel_threshold) ? 1.0 : 0.0;
    
    output.write(float4(edge, edge, edge, 1.0), gid);
}

// 锐化核
kernel void sharpen(texture2d<float, access::read> input [[texture(0)]],
                     texture2d<float, access::write> output [[texture(1)]],
                     constant float& strength [[buffer(0)]],
                     uint2 gid [[thread_position_in_grid]]) {
    
    if (gid.x < 1 || gid.y < 1) {
        output.write(input.read(gid), gid);
        return;
    }
    
    float3 center = input.read(gid).rgb;
    float3 blur = input.read(gid + int2(-1, 0)).rgb +
                  input.read(gid + int2( 1, 0)).rgb +
                  input.read(gid + int2( 0,-1)).rgb +
                  input.read(gid + int2( 0, 1)).rgb;
    blur *= 0.25;
    
    float3 sharpened = center + (center - blur) * strength;
    
    output.write(float4(clamp(sharpened, 0.0, 1.0), 1.0), gid);
}

// HDR Tone Mapping
kernel void hdr_tonemap(texture2d<float, access::read> input [[texture(0)]],
                        texture2d<float, access::write> output [[texture(1)]],
                        constant float& exposure [[buffer(0)]],
                        uint2 gid [[thread_position_in_grid]]) {
    
    float4 hdr = input.read(gid);
    
    // Reinhard Tone Mapping
    float3 mapped = 1.0 - exp(-hdr.rgb * exposure);
    
    // Gamma校正
    mapped = pow(mapped, float3(1.0/2.2));
    
    output.write(float4(mapped, hdr.a), gid);
}
)";

// ============================================================================
// Metal Compute 上下文
// ============================================================================

struct MetalComputeContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLLibrary> library;
    
    // 管线状态
    id<MTLComputePipelineState> main_pipeline;
    id<MTLComputePipelineState> sobel_pipeline;
    id<MTLComputePipelineState> sharpen_pipeline;
    id<MTLComputePipelineState> hdr_pipeline;
    
    // 纹理
    id<MTLTexture> input_texture;
    id<MTLTexture> output_texture;
    id<MTLTexture> gsdf_lut_texture;
    
    // 性能统计
    uint64_t total_frames;
    int64_t total_latency_us;
    
    MetalComputeContext() : device(nil),
                           command_queue(nil),
                           library(nil),
                           total_frames(0),
                           total_latency_us(0) {}
};

static MetalComputeContext* g_metal_ctx = nullptr;

// ============================================================================
// Metal Compute 初始化
// ============================================================================

int metal_compute_init(void) {
    if (g_metal_ctx) return 0;
    
    auto* ctx = new MetalComputeContext();
    
    // 获取默认设备
    ctx->device = MTLCreateSystemDefaultDevice();
    if (!ctx->device) {
        fprintf(stderr, "[Metal] No GPU found\n");
        delete ctx;
        return -1;
    }
    
    fprintf(stderr, "[Metal] GPU: %s\n", [[ctx->device name] UTF8String]);
    
    // 命令队列
    ctx->command_queue = [ctx->device newCommandQueue];
    if (!ctx->command_queue) {
        fprintf(stderr, "[Metal] Failed to create command queue\n");
        delete ctx;
        return -1;
    }
    
    // 编译Shader
    NSError* error = nil;
    ctx->library = [ctx->device newLibraryWithSource:
                   [NSString stringWithUTF8String:MAIN_COMPUTE_SHADER]
                                            options:nil
                                              error:&error];
    if (!ctx->library) {
        fprintf(stderr, "[Metal] Shader compilation failed: %s\n",
                [[error localizedDescription] UTF8String]);
        delete ctx;
        return -1;
    }
    
    // 创建管线
    auto create_pipeline = [&](const char* name, id<MTLComputePipelineState>& pipeline) {
        id<MTLFunction> func = [ctx->library newFunctionWithName:
                               [NSString stringWithUTF8String:name]];
        if (!func) {
            fprintf(stderr, "[Metal] Function %s not found\n", name);
            return false;
        }
        
        pipeline = [ctx->device newComputePipelineStateWithFunction:func error:&error];
        if (!pipeline) {
            fprintf(stderr, "[Metal] Pipeline creation failed for %s: %s\n",
                    name, [[error localizedDescription] UTF8String]);
            return false;
        }
        return true;
    };
    
    if (!create_pipeline("process_frame", ctx->main_pipeline)) {
        delete ctx;
        return -1;
    }
    create_pipeline("sobel_edge", ctx->sobel_pipeline);
    create_pipeline("sharpen", ctx->sharpen_pipeline);
    create_pipeline("hdr_tonemap", ctx->hdr_pipeline);
    
    g_metal_ctx = ctx;
    fprintf(stderr, "[Metal] Compute initialized successfully\n");
    return 0;
}

void metal_compute_shutdown(void) {
    if (!g_metal_ctx) return;
    
    g_metal_ctx->main_pipeline = nil;
    g_metal_ctx->sobel_pipeline = nil;
    g_metal_ctx->sharpen_pipeline = nil;
    g_metal_ctx->hdr_pipeline = nil;
    g_metal_ctx->input_texture = nil;
    g_metal_ctx->output_texture = nil;
    g_metal_ctx->gsdf_lut_texture = nil;
    g_metal_ctx->command_queue = nil;
    g_metal_ctx->library = nil;
    g_metal_ctx->device = nil;
    
    delete g_metal_ctx;
    g_metal_ctx = nullptr;
}

// ============================================================================
// Metal GPU Pipeline 实现
// ============================================================================

int metal_gpu_pipeline_create(GPUPipeline** pipeline, const GPUPipelineConfig* config) {
    if (!pipeline || !config) return -1;
    
    if (metal_compute_init() != 0) {
        return -1;
    }
    
    auto* p = new (std::nothrow) GPUPipeline();
    if (!p) return -1;
    
    p->config = *config;
    p->config.backend = GPU_BACKEND_METAL;
    p->total_frames = 0;
    p->last_latency_us = 0;
    
    *pipeline = p;
    return 0;
}

int metal_gpu_pipeline_process(GPUPipeline* pipeline,
                               const uint8_t* input_rgba,
                               uint8_t* output_rgba,
                               uint32_t width,
                               uint32_t height,
                               const GPUPipelineParams* params) {
    if (!g_metal_ctx || !input_rgba || !output_rgba || !params) return -1;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    @autoreleasepool {
        id<MTLCommandBuffer> command_buffer = 
            [g_metal_ctx->command_queue commandBuffer];
        
        // 创建临时纹理
        MTLTextureDescriptor* tex_desc = [MTLTextureDescriptor new];
        tex_desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
        tex_desc.width = width;
        tex_desc.height = height;
        tex_desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        
        id<MTLTexture> input_tex = [g_metal_ctx->device newTextureWithDescriptor:tex_desc];
        id<MTLTexture> output_tex = [g_metal_ctx->device newTextureWithDescriptor:tex_desc];
        
        // 上传输入数据
        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [input_tex replaceRegion:region mipmapLevel:0 
                     withBytes:input_rgba bytesPerRow:width * 4];
        
        // 编码计算
        id<MTLComputeCommandEncoder> encoder = 
            [command_buffer computeCommandEncoder];
        
        [encoder setComputePipelineState:g_metal_ctx->main_pipeline];
        [encoder setTexture:input_tex atIndex:0];
        [encoder setTexture:output_tex atIndex:1];
        
        // 设置参数
        struct {
            uint32_t width;
            uint32_t height;
            float brightness;
            float contrast;
            float saturation;
            uint32_t enable_gsdf;
            uint32_t enable_bloodless;
            float blood_suppress;
            float tissue_enhance;
            uint32_t enable_sobel;
            float sobel_threshold;
            uint32_t enable_hdr;
            uint32_t mode;
        } params_out;
        
        params_out.width = width;
        params_out.height = height;
        params_out.brightness = params->brightness;
        params_out.contrast = params->contrast;
        params_out.saturation = params->saturation;
        params_out.enable_gsdf = params->enable_hdr;
        params_out.enable_bloodless = params->enable_bloodless;
        params_out.blood_suppress = params->bloodless_strength;
        params_out.tissue_enhance = params->sharpness;
        params_out.enable_sobel = 0;
        params_out.sobel_threshold = params->edge_threshold;
        params_out.enable_hdr = params->enable_hdr;
        params_out.mode = params->mode;
        
        [encoder setBytes:&params_out length:sizeof(params_out) atIndex:0];
        
        // 分发线程组
        MTSize threadgroup_size = MTSizeMake(16, 16, 1);
        MTSize grid_size = MTSizeMake(width, height, 1);
        [encoder dispatchThreadgroups:MTLSizeMake((grid_size.width + 15) / 16,
                                                   (grid_size.height + 15) / 16,
                                                   1)
                 threadsPerThreadgroup:threadgroup_size];
        
        [encoder endEncoding];
        
        // 读取输出
        [command_buffer addCompletedHandler:^(id<MTLCommandBuffer>) {
            MTLRegion out_region = MTLRegionMake2D(0, 0, width, height);
            [output_tex getBytes:output_rgba bytesPerRow:width * 4
                        fromRegion:out_region mipmapLevel:0];
        }];
        
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        
        // 清理
        input_tex = nil;
        output_tex = nil;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    pipeline->last_latency_us = duration.count();
    pipeline->total_frames++;
    g_metal_ctx->total_frames++;
    g_metal_ctx->total_latency_us += duration.count();
    
    return 0;
}

bool metal_gpu_pipeline_is_available(void) {
    return MTLCreateSystemDefaultDevice() != nil;
}

#endif // __APPLE__
