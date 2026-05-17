/**
 * @file vulkan_compute.cpp
 * @brief Vulkan Compute Shader 实现 - GPU加速图像处理
 * 
 * 性能目标: 1920x1080 @ <10ms/frame
 */

#ifdef MEDICALDISPLAY_ENABLE_VULKAN

#include "gpu_pipeline.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <vulkan/vulkan.h>

// ============================================================================
// Vulkan Compute Shader 源代码
// ============================================================================

static const char* COMPUTE_SHADER_GLSL = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
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
} pc;

// 输入输出图像
layout(binding = 0, rgba8) uniform readonly image2D inputImage;
layout(binding = 1, rgba8) uniform writeonly image2D outputImage;

// GSDF LUT (binding 2)
layout(binding = 2) uniform sampler1D gsdfLut;

// 辅助函数
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float rgb2gray(vec3 c) {
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    
    if (pos.x >= int(pc.width) || pos.y >= int(pc.height)) {
        return;
    }
    
    vec2 uv = vec2(pos) / vec2(pc.width, pc.height);
    
    // 读取输入像素
    vec4 pixel = imageLoad(inputImage, pos);
    vec3 rgb = pixel.rgb;
    
    // 1. 亮度/对比度调整
    if (pc.brightness != 0.0 || pc.contrast != 1.0) {
        rgb = (rgb - 0.5) * pc.contrast + 0.5 + pc.brightness;
        rgb = clamp(rgb, 0.0, 1.0);
    }
    
    // 2. 饱和度调整
    if (pc.saturation != 1.0) {
        vec3 hsv = rgb2hsv(rgb);
        hsv.y *= pc.saturation;
        rgb = hsv2rgb(hsv);
    }
    
    // 3. GSDF查表
    if (pc.enable_gsdf == 1) {
        float gray = rgb2gray(rgb);
        float gsdf_val = texture(gsdfLut, gray / 255.0).r;
        rgb *= (gsdf_val / max(gray, 0.001));
    }
    
    // 4. 无血术野增强
    if (pc.enable_bloodless == 1) {
        // 血色检测
        float blood_score = rgb.r * 0.5 - rgb.g * 0.3 - rgb.b * 0.2;
        float blood_mask = clamp(blood_score, 0.0, 1.0);
        
        // 抑制血色
        rgb.r *= 1.0 - blood_mask * pc.blood_suppress * 0.3;
        rgb.g *= 1.0 + blood_mask * pc.blood_suppress * 0.2;
        rgb.b *= 1.0 + blood_mask * pc.blood_suppress * 0.2;
        
        // 组织对比度增强
        float gray = rgb2gray(rgb);
        rgb = mix(vec3(gray), rgb, 1.0 + pc.tissue_enhance * 0.2);
    }
    
    // 5. HDR Tone Mapping
    if (pc.enable_hdr == 1) {
        rgb = 1.0 - exp(-rgb * 1.0);  // Reinhard
        rgb = pow(rgb, vec3(1.0/2.2));  // Gamma
    }
    
    // 写入输出
    imageStore(outputImage, pos, vec4(clamp(rgb, 0.0, 1.0), 1.0));
}
)";

// Sobel边缘检测着色器
static const char* SOBEL_SHADER_GLSL = R"(
#version 450

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
    float threshold;
} pc;

layout(binding = 0, rgba8) uniform readonly image2D inputImage;
layout(binding = 1, r8) uniform writeonly image2D edgeImage;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    
    if (pos.x < 1 || pos.x >= int(pc.width) - 1 ||
        pos.y < 1 || pos.y >= int(pc.height) - 1) {
        imageStore(edgeImage, pos, vec4(0.0));
        return;
    }
    
    vec2 tex_size = vec2(pc.width, pc.height);
    
    // Sobel X
    float gx = 0.0;
    gx -= rgb2gray(imageLoad(inputImage, pos + ivec2(-1,-1)).rgb) * 1.0;
    gx += rgb2gray(imageLoad(inputImage, pos + ivec2(1,-1)).rgb) * 1.0;
    gx -= rgb2gray(imageLoad(inputImage, pos + ivec2(-1,0)).rgb) * 2.0;
    gx += rgb2gray(imageLoad(inputImage, pos + ivec2(1,0)).rgb) * 2.0;
    gx -= rgb2gray(imageLoad(inputImage, pos + ivec2(-1,1)).rgb) * 1.0;
    gx += rgb2gray(imageLoad(inputImage, pos + ivec2(1,1)).rgb) * 1.0;
    
    // Sobel Y
    float gy = 0.0;
    gy -= rgb2gray(imageLoad(inputImage, pos + ivec2(-1,-1)).rgb) * 1.0;
    gy -= rgb2gray(imageLoad(inputImage, pos + ivec2(0,-1)).rgb) * 2.0;
    gy -= rgb2gray(imageLoad(inputImage, pos + ivec2(1,-1)).rgb) * 1.0;
    gy += rgb2gray(imageLoad(inputImage, pos + ivec2(-1,1)).rgb) * 1.0;
    gy += rgb2gray(imageLoad(inputImage, pos + ivec2(0,1)).rgb) * 2.0;
    gy += rgb2gray(imageLoad(inputImage, pos + ivec2(1,1)).rgb) * 1.0;
    
    float mag = sqrt(gx*gx + gy*gy);
    float edge = (mag > pc.threshold) ? 1.0 : 0.0;
    
    imageStore(edgeImage, pos, vec4(edge));
}

float rgb2gray(vec3 c) {
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}
)";

// ============================================================================
// Vulkan 上下文
// ============================================================================

struct VulkanComputeContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue compute_queue;
    int queue_family_index;
    
    // 描述符
    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet descriptor_set;
    
    // 计算管线
    VkPipeline main_pipeline;
    VkPipeline sobel_pipeline;
    VkPipelineLayout pipeline_layout;
    
    // 命令缓冲
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    
    // 同步
    VkFence fence;
    
    // 纹理
    VkImage input_image;
    VkImageView input_view;
    VkDeviceMemory input_memory;
    VkImage output_image;
    VkImageView output_view;
    VkDeviceMemory output_memory;
    
    // 统计
    uint64_t total_frames;
    int64_t total_latency_us;
    bool initialized;
    
    VulkanComputeContext() : instance(VK_NULL_HANDLE),
                            physical_device(VK_NULL_HANDLE),
                            device(VK_NULL_HANDLE),
                            total_frames(0),
                            total_latency_us(0),
                            initialized(false) {}
};

static VulkanComputeContext* g_vulkan_ctx = nullptr;

// ============================================================================
// Vulkan 辅助函数
// ============================================================================

static const char* get_device_extension_names[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME
};

static uint32_t find_memory_type(VkPhysicalDevice device,
                                 uint32_t type_filter,
                                 VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(device, &mem_props);
    
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && 
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

static bool create_shader_module(VkDevice device, const char* code, size_t code_size,
                                 VkShaderModule* module) {
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code_size;
    info.pCode = (const uint32_t*)code;
    
    return vkCreateShaderModule(device, &info, nullptr, module) == VK_SUCCESS;
}

// ============================================================================
// Vulkan Compute 初始化
// ============================================================================

int vulkan_compute_init(void) {
    if (g_vulkan_ctx && g_vulkan_ctx->initialized) return 0;
    
    auto* ctx = new VulkanComputeContext();
    
    // 应用层
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "MedicalDisplay Compute";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;
    
    // 实例
    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    instance_info.enabledLayerCount = 0;  // 验证层可选
    
    if (vkCreateInstance(&instance_info, nullptr, &ctx->instance) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to create instance\n");
        delete ctx;
        return -1;
    }
    
    // 物理设备
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, nullptr);
    if (device_count == 0) {
        fprintf(stderr, "[Vulkan] No GPU found\n");
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices.data());
    
    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        
        // 选择第一个可用的设备
        ctx->physical_device = dev;
        fprintf(stderr, "[Vulkan] Selected GPU: %s\n", props.deviceName);
        break;
    }
    
    if (!ctx->physical_device) {
        fprintf(stderr, "[Vulkan] No suitable GPU\n");
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    // 队列族
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &queue_family_count, queue_families.data());
    
    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            ctx->queue_family_index = i;
            break;
        }
    }
    
    // 设备
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = ctx->queue_family_index;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    
    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 2;
    device_info.ppEnabledExtensionNames = get_device_extension_names;
    
    if (vkCreateDevice(ctx->physical_device, &device_info, nullptr, &ctx->device) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to create device\n");
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    vkGetDeviceQueue(ctx->device, ctx->queue_family_index, 0, &ctx->compute_queue);
    
    // 命令池
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = ctx->queue_family_index;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    if (vkCreateCommandPool(ctx->device, &pool_info, nullptr, &ctx->command_pool) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to create command pool\n");
        vkDestroyDevice(ctx->device, nullptr);
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    // 命令缓冲
    VkCommandBufferAllocateInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    buf_info.commandPool = ctx->command_pool;
    buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    buf_info.commandBufferCount = 1;
    
    if (vkAllocateCommandBuffers(ctx->device, &buf_info, &ctx->command_buffer) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to allocate command buffer\n");
        vkDestroyCommandPool(ctx->device, ctx->command_pool);
        vkDestroyDevice(ctx->device, nullptr);
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    // 描述符布局
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    
    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 3;
    layout_info.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(ctx->device, &layout_info, nullptr, &ctx->descriptor_set_layout) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to create descriptor set layout\n");
        vkDestroyCommandPool(ctx->device, ctx->command_pool);
        vkDestroyDevice(ctx->device, nullptr);
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    // 描述符池
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
    };
    
    VkDescriptorPoolCreateInfo pool_create = {};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 1;
    pool_create.poolSizeCount = 2;
    pool_create.pPoolSizes = pool_sizes;
    
    if (vkCreateDescriptorPool(ctx->device, &pool_create, nullptr, &ctx->descriptor_pool) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to create descriptor pool\n");
        vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptor_set_layout, nullptr);
        vkDestroyCommandPool(ctx->device, ctx->command_pool);
        vkDestroyDevice(ctx->device, nullptr);
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    // 描述符集
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = ctx->descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &ctx->descriptor_set_layout;
    
    if (vkAllocateDescriptorSets(ctx->device, &alloc_info, &ctx->descriptor_set) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to allocate descriptor set\n");
        vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptor_set_layout, nullptr);
        vkDestroyCommandPool(ctx->device, ctx->command_pool);
        vkDestroyDevice(ctx->device, nullptr);
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    // 管线布局
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &ctx->descriptor_set_layout;
    
    // 推送常量
    VkPushConstantRange push_constant = {};
    push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant.offset = 0;
    push_constant.size = 256;  // 足够大的推送常量
    
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant;
    
    if (vkCreatePipelineLayout(ctx->device, &pipeline_layout_info, nullptr, &ctx->pipeline_layout) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to create pipeline layout\n");
        vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptor_set_layout, nullptr);
        vkDestroyCommandPool(ctx->device, ctx->command_pool);
        vkDestroyDevice(ctx->device, nullptr);
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    // 编译Shader
    VkShaderModule main_shader;
    if (!create_shader_module(ctx->device, COMPUTE_SHADER_GLSL, strlen(COMPUTE_SHADER_GLSL), &main_shader)) {
        fprintf(stderr, "[Vulkan] Failed to compile main shader\n");
    } else {
        VkPipelineShaderStageCreateInfo shader_stage = {};
        shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shader_stage.module = main_shader;
        shader_stage.pName = "main";
        
        VkComputePipelineCreateInfo pipeline_info = {};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = shader_stage;
        pipeline_info.layout = ctx->pipeline_layout;
        
        if (vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &ctx->main_pipeline) != VK_SUCCESS) {
            fprintf(stderr, "[Vulkan] Failed to create compute pipeline\n");
        }
        
        vkDestroyShaderModule(ctx->device, main_shader, nullptr);
    }
    
    // 同步
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    if (vkCreateFence(ctx->device, &fence_info, nullptr, &ctx->fence) != VK_SUCCESS) {
        fprintf(stderr, "[Vulkan] Failed to create fence\n");
        vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, nullptr);
        vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptor_set_layout, nullptr);
        vkDestroyCommandPool(ctx->device, ctx->command_pool);
        vkDestroyDevice(ctx->device, nullptr);
        vkDestroyInstance(ctx->instance, nullptr);
        delete ctx;
        return -1;
    }
    
    ctx->initialized = true;
    g_vulkan_ctx = ctx;
    fprintf(stderr, "[Vulkan] Compute initialized successfully\n");
    return 0;
}

void vulkan_compute_shutdown(void) {
    if (!g_vulkan_ctx) return;
    
    vkDeviceWaitIdle(g_vulkan_ctx->device);
    
    if (g_vulkan_ctx->fence) vkDestroyFence(g_vulkan_ctx->device, g_vulkan_ctx->fence, nullptr);
    if (g_vulkan_ctx->main_pipeline) vkDestroyPipeline(g_vulkan_ctx->device, g_vulkan_ctx->main_pipeline, nullptr);
    if (g_vulkan_ctx->pipeline_layout) vkDestroyPipelineLayout(g_vulkan_ctx->device, g_vulkan_ctx->pipeline_layout, nullptr);
    if (g_vulkan_ctx->descriptor_pool) vkDestroyDescriptorPool(g_vulkan_ctx->device, g_vulkan_ctx->descriptor_pool, nullptr);
    if (g_vulkan_ctx->descriptor_set_layout) vkDestroyDescriptorSetLayout(g_vulkan_ctx->device, g_vulkan_ctx->descriptor_set_layout, nullptr);
    if (g_vulkan_ctx->command_pool) vkDestroyCommandPool(g_vulkan_ctx->device, g_vulkan_ctx->command_pool, nullptr);
    if (g_vulkan_ctx->device) vkDestroyDevice(g_vulkan_ctx->device, nullptr);
    if (g_vulkan_ctx->instance) vkDestroyInstance(g_vulkan_ctx->instance, nullptr);
    
    delete g_vulkan_ctx;
    g_vulkan_ctx = nullptr;
}

// ============================================================================
// GPU Pipeline Vulkan 实现
// ============================================================================

int gpu_pipeline_process_frame(GPUPipeline* pipeline,
                               const uint8_t* input_rgba,
                               uint8_t* output_rgba,
                               uint32_t width,
                               uint32_t height,
                               const GPUPipelineParams* params) {
    // 如果Vulkan不可用，使用CPU fallback
    if (!g_vulkan_ctx || !g_vulkan_ctx->initialized) {
        return gpu_pipeline_process_frame_cpu_fallback(pipeline, input_rgba, output_rgba,
                                                      width, height, params);
    }
    
    auto* ctx = g_vulkan_ctx;
    auto start = std::chrono::high_resolution_clock::now();
    
    // 创建临时纹理
    VkImageCreateInfo img_info = {};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent = {width, height, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    // 输入图像
    VkImage input_img;
    VkDeviceMemory input_mem;
    
    if (vkCreateImage(ctx->device, &img_info, nullptr, &input_img) != VK_SUCCESS) {
        return -1;
    }
    
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, input_img, &mem_reqs);
    
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(ctx->device, &alloc_info, nullptr, &input_mem) != VK_SUCCESS) {
        vkDestroyImage(ctx->device, input_img, nullptr);
        return -1;
    }
    
    vkBindImageMemory(ctx->device, input_img, input_mem, 0);
    
    // 复制输入数据
    void* mapped;
    vkMapMemory(ctx->device, input_mem, 0, width * height * 4, 0, &mapped);
    memcpy(mapped, input_rgba, width * height * 4);
    vkUnmapMemory(ctx->device, input_mem);
    
    // 创建图像视图
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = input_img;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    VkImageView input_view;
    if (vkCreateImageView(ctx->device, &view_info, nullptr, &input_view) != VK_SUCCESS) {
        vkFreeMemory(ctx->device, input_mem, nullptr);
        vkDestroyImage(ctx->device, input_img, nullptr);
        return -1;
    }
    
    // 输出图像
    VkImage output_img;
    VkDeviceMemory output_mem;
    
    if (vkCreateImage(ctx->device, &img_info, nullptr, &output_img) != VK_SUCCESS) {
        vkDestroyImageView(ctx->device, input_view, nullptr);
        vkFreeMemory(ctx->device, input_mem, nullptr);
        vkDestroyImage(ctx->device, input_img, nullptr);
        return -1;
    }
    
    vkGetImageMemoryRequirements(ctx->device, output_img, &mem_reqs);
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(ctx->device, &alloc_info, nullptr, &output_mem) != VK_SUCCESS) {
        vkDestroyImageView(ctx->device, input_view, nullptr);
        vkFreeMemory(ctx->device, input_mem, nullptr);
        vkDestroyImage(ctx->device, input_img, nullptr);
        vkDestroyImage(ctx->device, output_img, nullptr);
        return -1;
    }
    
    vkBindImageMemory(ctx->device, output_img, output_mem, 0);
    
    VkImageView output_view;
    view_info.image = output_img;
    if (vkCreateImageView(ctx->device, &view_info, nullptr, &output_view) != VK_SUCCESS) {
        vkDestroyImageView(ctx->device, input_view, nullptr);
        vkFreeMemory(ctx->device, input_mem, nullptr);
        vkFreeMemory(ctx->device, output_mem, nullptr);
        vkDestroyImage(ctx->device, input_img, nullptr);
        vkDestroyImage(ctx->device, output_img, nullptr);
        return -1;
    }
    
    // 更新描述符集
    VkDescriptorImageInfo input_desc = {};
    input_desc.imageView = input_view;
    input_desc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkDescriptorImageInfo output_desc = {};
    output_desc.imageView = output_view;
    output_desc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ctx->descriptor_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &input_desc, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ctx->descriptor_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &output_desc, nullptr, nullptr}
    };
    
    vkUpdateDescriptorSets(ctx->device, 2, writes, 0, nullptr);
    
    // 记录命令
    vkResetCommandBuffer(ctx->command_buffer, 0);
    
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(ctx->command_buffer, &begin_info);
    
    // 图像布局转换
    VkImageMemoryBarrier barrier1 = {};
    barrier1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier1.srcAccessMask = 0;
    barrier1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier1.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier1.image = input_img;
    barrier1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    vkCmdPipelineBarrier(ctx->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);
    
    barrier1.image = output_img;
    vkCmdPipelineBarrier(ctx->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);
    
    // 绑定管线
    vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->main_pipeline);
    vkCmdBindDescriptorSets(ctx->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_layout, 0, 1, &ctx->descriptor_set, 0, nullptr);
    
    // 推送常量
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
    } push_constants;
    
    push_constants.width = width;
    push_constants.height = height;
    push_constants.brightness = params->brightness;
    push_constants.contrast = params->contrast;
    push_constants.saturation = params->saturation;
    push_constants.enable_gsdf = params->enable_hdr;
    push_constants.enable_bloodless = params->enable_bloodless;
    push_constants.blood_suppress = params->bloodless_strength;
    push_constants.tissue_enhance = params->sharpness;
    push_constants.enable_sobel = 0;
    push_constants.sobel_threshold = params->edge_threshold;
    push_constants.enable_hdr = params->enable_hdr;
    push_constants.mode = params->mode;
    
    vkCmdPushConstants(ctx->command_buffer, ctx->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
    
    // 分发
    vkCmdDispatch(ctx->command_buffer, (width + 15) / 16, (height + 15) / 16, 1);
    
    // 复制输出
    VkImageMemoryBarrier barrier2 = {};
    barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier2.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier2.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier2.image = output_img;
    barrier2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    
    vkCmdPipelineBarrier(ctx->command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);
    
    vkEndCommandBuffer(ctx->command_buffer);
    
    // 提交
    vkWaitForFences(ctx->device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->device, 1, &ctx->fence);
    
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &ctx->command_buffer;
    
    vkQueueSubmit(ctx->compute_queue, 1, &submit_info, ctx->fence);
    vkWaitForFences(ctx->device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
    
    // 读取输出 (简化实现，实际应使用 staging buffer)
    vkMapMemory(ctx->device, output_mem, 0, width * height * 4, 0, &mapped);
    memcpy(output_rgba, mapped, width * height * 4);
    vkUnmapMemory(ctx->device, output_mem);
    
    // 清理
    vkDestroyImageView(ctx->device, input_view, nullptr);
    vkDestroyImageView(ctx->device, output_view, nullptr);
    vkFreeMemory(ctx->device, input_mem, nullptr);
    vkFreeMemory(ctx->device, output_mem, nullptr);
    vkDestroyImage(ctx->device, input_img, nullptr);
    vkDestroyImage(ctx->device, output_img, nullptr);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    pipeline->last_latency_us = duration.count();
    pipeline->total_frames++;
    ctx->total_frames++;
    ctx->total_latency_us += duration.count();
    
    return 0;
}

bool gpu_pipeline_is_available(GPUBackendType preferred_backend) {
    if (preferred_backend == GPU_BACKEND_VULKAN) {
        return vulkan_compute_init() == 0;
    }
    return false;
}

#endif // MEDICALDISPLAY_ENABLE_VULKAN
