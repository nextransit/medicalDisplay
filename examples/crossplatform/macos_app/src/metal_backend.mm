/**
 * Metal 渲染后端 — 医学影像 GPU 加速处理
 *
 * 核心功能:
 *   - Metal Compute Shader 实时图像生成
 *   - DICOM GSDF 感知亮度校准
 *   - 亮度/对比度/饱和度实时调整
 *   - 多模态医学影像测试图案
 */

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include <cstdio>
#include <cstring>
#include <cmath>

// ---- Metal Shader 源码 ----
static const char* kShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct RenderParams {
    uint   width;
    uint   height;
    float  brightness;    // [-0.5, 0.5]
    float  contrast;      // [0.5, 2.0]
    float  saturation;    // [0.0, 2.0]
    int    enableGsdf;    // DICOM GSDF 校准
    int    enableBloodless;
    float  bloodSuppress;
    float  tissueEnhance;
    int    modality;      // 0:CT 1:MRI 2:XRay 3:Ultrasound
};

// RGB ↔ HSV 色彩空间转换
float3 rgb_to_hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv_to_rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// DICOM Part 14 GSDF 感知亮度变换
float gsdf_perceptual(float linear_gray) {
    // 将归一化灰度 [0,1] 映射到感知均匀的 JND 空间
    float jnd_index = linear_gray * 1023.0;  // 10-bit JND 索引
    // GSDF 公式: log10(L) = (a + c·ln(j) + e·ln(j)² + g·ln(j)³ + m·ln(j)⁴)
    //                       / (1 + b·ln(j) + d·ln(j)² + f·ln(j)³ + h·ln(j)⁴ + k·ln(j)⁵)
    float ln_j = log(max(jnd_index, 1.0f));
    float ln_j2 = ln_j * ln_j;
    float ln_j3 = ln_j2 * ln_j;
    float ln_j4 = ln_j3 * ln_j;
    float ln_j5 = ln_j4 * ln_j;

    float num = -1.3011877f + (-2.5840191e-2f) * ln_j + (8.0242636e-2f) * ln_j2
              + (-1.0320229e-2f) * ln_j3 + (1.3646699e-3f) * ln_j4;
    float den = 1.0f + (7.4950716e-2f) * ln_j + (8.4375435e-3f) * ln_j2
              + (-1.1845445e-4f) * ln_j3 + (1.7320966e-5f) * ln_j4 + (-7.6472253e-7f) * ln_j5;
    float log10_L = num / den;

    // 归一化到 [0, 1]，假设最大亮度 500 cd/m²
    float L = pow(10.0f, log10_L);
    return clamp(L / 500.0f, 0.0f, 1.0f);
}

// 生成医学影像测试图案
float3 generate_test_pattern(float x, float y, int modality) {
    float cx = x - 0.5;
    float cy = y - 0.5;
    float dist = sqrt(cx * cx + cy * cy);
    float angle = atan2(cy, cx);

    switch (modality) {
        case 0: { // CT — 径向渐变 + 解剖结构模拟
            float skull = smoothstep(0.38, 0.42, dist) - smoothstep(0.44, 0.48, dist);
            float brain = smoothstep(0.05, 0.35, dist) * (1.0 - smoothstep(0.35, 0.40, dist));
            float ventricle = exp(-dist * dist * 80.0) * 0.6;
            float tissue = sin(x * 12.0) * cos(y * 15.0) * 0.08 * (1.0 - skull);
            float val = skull * 0.95 + brain * 0.55 + ventricle * 0.3 + tissue;
            return float3(val);
        }
        case 1: { // MRI — 脑组织对比
            float gm = smoothstep(0.08, 0.30, dist) - smoothstep(0.30, 0.35, dist);
            float wm = smoothstep(0.05, 0.20, dist) * (1.0 - smoothstep(0.20, 0.25, dist));
            float csf = exp(-dist * dist * 100.0) * 0.7;
            float folding = sin(angle * 8.0 + dist * 20.0) * 0.06;
            float val = gm * 0.55 + wm * 0.75 + csf * 0.25 + folding;
            return float3(val);
        }
        case 2: { // X-Ray — 胸部模拟
            float lung_l = exp(-((x - 0.38) * (x - 0.38) * 80.0 + (y - 0.48) * (y - 0.48) * 50.0));
            float lung_r = exp(-((x - 0.62) * (x - 0.62) * 80.0 + (y - 0.48) * (y - 0.48) * 50.0));
            float spine = exp(-((x - 0.50) * (x - 0.50) * 200.0)) * exp(-y * 1.5);
            float ribs_h = sin(y * 18.0) * 0.3 * exp(-abs(x - 0.5) * 0.5);
            float val = 0.85 - lung_l * 0.5 - lung_r * 0.5 + spine * 0.25 + ribs_h * 0.1;
            return float3(clamp(val, 0.0f, 1.0f));
        }
        case 3: { // 超声 — 扇形扫描
            float sector = (abs(angle) < 0.6) ? (1.0 - dist * 1.4) : 0.0;
            float speckle = (sin(x * 80.0 + y * 95.0) * sin(y * 73.0 - x * 67.0) * 0.5 + 0.5) * 0.15;
            float tissue = sector * (0.3 + speckle);
            float vessel = exp(-((x - 0.45) * (x - 0.45) + (y - 0.55) * (y - 0.55)) * 200.0) * 0.4;
            float val = tissue + vessel;
            return float3(clamp(val, 0.0f, 1.0f));
        }
        default:
            return float3(0.5);
    }
}

// 主 Compute Kernel
kernel void render_medical(
    texture2d<float, access::write> output [[texture(0)]],
    constant RenderParams& params        [[buffer(0)]],
    uint2 gid                            [[thread_position_in_grid]])
{
    if (gid.x >= params.width || gid.y >= params.height) return;

    float x = (float)gid.x / (float)params.width;
    float y = (float)gid.y / (float)params.height;

    float3 rgb = generate_test_pattern(x, y, params.modality);

    // 亮度 + 对比度
    if (params.brightness != 0.0 || params.contrast != 1.0) {
        rgb = (rgb - 0.5) * params.contrast + 0.5 + params.brightness;
        rgb = clamp(rgb, 0.0f, 1.0f);
    }

    // 饱和度
    if (params.saturation != 1.0) {
        float3 hsv = rgb_to_hsv(rgb);
        hsv.y *= params.saturation;
        rgb = hsv_to_rgb(hsv);
    }

    // GSDF 感知校准
    if (params.enableGsdf) {
        float gray = 0.299f * rgb.r + 0.587f * rgb.g + 0.114f * rgb.b;
        float perceptual = gsdf_perceptual(gray);
        float scale = perceptual / max(gray, 0.001f);
        rgb *= scale;
        rgb = clamp(rgb, 0.0f, 1.0f);
    }

    // 无血模式
    if (params.enableBloodless) {
        float3 hsv = rgb_to_hsv(rgb);
        if (hsv.x <= 0.083f && hsv.y > 0.3f) {
            hsv.y *= (1.0f - params.bloodSuppress * 0.7f);
            hsv.z *= (1.0f + params.tissueEnhance * 0.2f);
        }
        rgb = hsv_to_rgb(hsv);
        rgb = clamp(rgb, 0.0f, 1.0f);
    }

    output.write(float4(rgb, 1.0f), gid);
}
)";

// ---- C++ 接口 ----
extern "C" {

// 统一上下文结构体（避免每个函数重复定义）
struct MetalContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> pipeline;
    id<MTLTexture> texture;
    int width;
    int height;
};

struct RenderParamsC {
    unsigned int width;
    unsigned int height;
    float brightness;
    float contrast;
    float saturation;
    int enableGsdf;
    int enableBloodless;
    float bloodSuppress;
    float tissueEnhance;
    int modality;
};

void* metal_init(int width, int height) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        fprintf(stderr, "[Metal] 设备不可用\n");
        return nullptr;
    }
    printf("[Metal] 设备: %s\n", [[device name] UTF8String]);

    // 编译 Shader
    NSError* error = nil;
    NSString* src = [NSString stringWithUTF8String:kShaderSource];
    id<MTLLibrary> library = [device newLibraryWithSource:src options:nil error:&error];
    if (error) {
        fprintf(stderr, "[Metal] Shader 编译错误: %s\n", [[error localizedDescription] UTF8String]);
        return nullptr;
    }

    id<MTLFunction> func = [library newFunctionWithName:@"render_medical"];
    if (!func) {
        fprintf(stderr, "[Metal] Kernel 函数未找到\n");
        return nullptr;
    }

    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:func error:&error];
    if (error) {
        fprintf(stderr, "[Metal] Pipeline 创建失败: %s\n", [[error localizedDescription] UTF8String]);
        return nullptr;
    }

    id<MTLCommandQueue> queue = [device newCommandQueue];

    // 创建纹理
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:width height:height mipmapped:NO];
    desc.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];

    auto* ctx = new MetalContext();
    ctx->device = device;
    ctx->queue = queue;
    ctx->pipeline = pipeline;
    ctx->texture = texture;
    ctx->width = width;
    ctx->height = height;

    printf("[Metal] 初始化完成 (%dx%d)\n", width, height);
    return ctx;
}

void metal_render(void* ctx_ptr, const RenderParamsC* params, void* out_pixels) {
    if (!ctx_ptr || !params) return;
    auto* ctx = (MetalContext*)ctx_ptr;

    @autoreleasepool {
        id<MTLCommandBuffer> cmdBuf = [ctx->queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];

        [enc setComputePipelineState:ctx->pipeline];
        [enc setTexture:ctx->texture atIndex:0];
        [enc setBytes:params length:sizeof(RenderParamsC) atIndex:0];

        MTLSize threadsPerGroup = MTLSizeMake(16, 16, 1);
        MTLSize numGroups = MTLSizeMake(
            (params->width + 15) / 16,
            (params->height + 15) / 16,
            1);

        [enc dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
        [enc endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];

        // 读回像素
        if (out_pixels) {
            [ctx->texture getBytes:out_pixels
                       bytesPerRow:params->width * 4
                        fromRegion:MTLRegionMake2D(0, 0, params->width, params->height)
                       mipmapLevel:0];
        }
    }
}

void metal_destroy(void* ctx_ptr) {
    if (!ctx_ptr) return;
    delete (MetalContext*)ctx_ptr;
    printf("[Metal] 资源已释放\n");
}

int metal_get_width(void* ctx_ptr) {
    if (!ctx_ptr) return 0;
    return ((MetalContext*)ctx_ptr)->width;
}

int metal_get_height(void* ctx_ptr) {
    if (!ctx_ptr) return 0;
    return ((MetalContext*)ctx_ptr)->height;
}

} // extern "C"
