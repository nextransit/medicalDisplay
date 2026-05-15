/**
 * @file vulkan_renderer.cpp
 * @brief Vulkan-based Medical Image Renderer
 */

#include "vulkan_renderer.h"
#include <cstring>
#include <cstdio>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Internal Structures
// ============================================================================

struct VulkanRenderer {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    int graphics_queue_family;
    int present_queue_family;
    
    // Surface
    VkSurfaceKHR surface;
    VkFormat surface_format;
    VkColorSpaceKHR surface_colorspace;
    
    // Swap chain
    VkSwapchainKHR swap_chain;
    std::vector<VkImage> swap_chain_images;
    std::vector<VkImageView> swap_chain_image_views;
    VkExtent2D swap_chain_extent;
    
    // Render pass
    VkRenderPass render_pass;
    
    // Pipeline
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;
    
    // Framebuffers
    std::vector<VkFramebuffer> swap_chain_framebuffers;
    
    // Command pool
    VkCommandPool command_pool;
    std::vector<VkCommandBuffer> command_buffers;
    
    // Sync
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    
    // Texture
    VkImage texture_image;
    VkDeviceMemory texture_memory;
    VkImageView texture_image_view;
    VkSampler texture_sampler;
    
    // Descriptor set
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    std::vector<VkDescriptorSet> descriptor_sets;
    
    // GSDF LUT
    VkBuffer gsdf_lut_buffer;
    VkDeviceMemory gsdf_lut_memory;
    VkImageView gsdf_lut_view;
    
    // State
    uint32_t current_frame;
    bool initialized;
    
    // Window
    void* window;
    uint32_t width;
    uint32_t height;
};

// ============================================================================
// Forward Declarations
// ============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT*, void*);

static const char* getVkResultString(VkResult result);

// ============================================================================
// Instance Creation
// ============================================================================

void* vulkan_create(VkInstance instance, VkPhysicalDevice physical_device) {
    if (!instance || !physical_device) return nullptr;
    
    auto* renderer = new VulkanRenderer;
    if (!renderer) return nullptr;
    
    memset(renderer, 0, sizeof(VulkanRenderer));
    renderer->instance = instance;
    renderer->physical_device = physical_device;
    renderer->current_frame = 0;
    renderer->initialized = false;
    
    return renderer;
}

void vulkan_destroy(void* renderer) {
    if (!renderer) return;
    
    auto* r = static_cast<VulkanRenderer*>(renderer);
    
    if (r->device) {
        vkDeviceWaitIdle(r->device);
        
        // Destroy texture
        if (r->texture_sampler) vkDestroySampler(r->device, r->texture_sampler, nullptr);
        if (r->texture_image_view) vkDestroyImageView(r->device, r->texture_image_view, nullptr);
        if (r->texture_image) vkDestroyImage(r->device, r->texture_image, nullptr);
        if (r->texture_memory) vkDeviceMemoryFree(r->device, r->texture_memory, nullptr);
        
        // Destroy GSDF LUT
        if (r->gsdf_lut_buffer) vkDestroyBuffer(r->device, r->gsdf_lut_buffer, nullptr);
        if (r->gsdf_lut_memory) vkDeviceMemoryFree(r->device, r->gsdf_lut_memory, nullptr);
        if (r->gsdf_lut_view) vkDestroyImageView(r->device, r->gsdf_lut_view, nullptr);
        
        // Destroy descriptors
        if (r->descriptor_pool) vkDestroyDescriptorPool(r->device, r->descriptor_pool, nullptr);
        if (r->descriptor_set_layout) vkDestroyDescriptorSetLayout(r->device, r->descriptor_set_layout, nullptr);
        
        // Destroy framebuffers
        for (auto framebuffer : r->swap_chain_framebuffers) {
            vkDestroyFramebuffer(r->device, framebuffer, nullptr);
        }
        
        // Destroy pipeline
        if (r->graphics_pipeline) vkDestroyPipeline(r->device, r->graphics_pipeline, nullptr);
        if (r->pipeline_layout) vkDestroyPipelineLayout(r->device, r->pipeline_layout, nullptr);
        if (r->render_pass) vkDestroyRenderPass(r->device, r->render_pass, nullptr);
        
        // Destroy swap chain
        for (auto view : r->swap_chain_image_views) {
            vkDestroyImageView(r->device, view, nullptr);
        }
        if (r->swap_chain) vkDestroySwapchainKHR(r->device, r->swap_chain, nullptr);
        
        // Destroy command buffers and pool
        if (!r->command_buffers.empty()) {
            vkFreeCommandBuffers(r->device, r->command_pool, 
                                 (uint32_t)r->command_buffers.size(), 
                                 r->command_buffers.data());
        }
        if (r->command_pool) vkDestroyCommandPool(r->device, r->command_pool, nullptr);
        
        // Destroy sync objects
        for (size_t i = 0; i < r->image_available_semaphores.size(); i++) {
            vkDestroySemaphore(r->device, r->image_available_semaphores[i], nullptr);
            vkDestroySemaphore(r->device, r->render_finished_semaphores[i], nullptr);
            vkDestroyFence(r->device, r->in_flight_fences[i], nullptr);
        }
        
        vkDestroyDevice(r->device, nullptr);
    }
    
    if (r->surface) vkDestroySurfaceKHR(r->instance, r->surface, nullptr);
    
    delete r;
}

// ============================================================================
// Queue Family Finding
// ============================================================================

static int findQueueFamily(VkPhysicalDevice device, VkSurfaceKHR surface, bool need_graphics) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
    
    std::vector<VkQueueFamilyProperties> families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());
    
    int graphics_family = -1;
    int present_family = -1;
    
    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_family = i;
        }
        
        VkBool32 present_support = false;
        if (surface) {
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
        }
        
        if (present_support) {
            present_family = i;
        }
        
        if (graphics_family >= 0 && present_family >= 0) {
            break;
        }
    }
    
    return need_graphics ? graphics_family : present_family;
}

// ============================================================================
// Pipeline Setup
// ============================================================================

static VkShaderModule createShaderModule(VkDevice device, const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = code;
    
    VkShaderModule module;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return module;
}

int vulkan_create_pipeline(void* renderer, const uint32_t* vert_shader, size_t vert_size,
                           const uint32_t* frag_shader, size_t frag_size) {
    if (!renderer) return -1;
    
    auto* r = static_cast<VulkanRenderer*>(renderer);
    if (!r->device) return -1;
    
    // Create shader modules
    VkShaderModule vert_module = createShaderModule(r->device, vert_shader, vert_size);
    VkShaderModule frag_module = createShaderModule(r->device, frag_shader, frag_size);
    
    if (!vert_module || !frag_module) {
        return -1;
    }
    
    // Shader stages
    VkPipelineShaderStageCreateInfo vertStageInfo = {};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vert_module;
    vertStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragStageInfo = {};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = frag_module;
    fragStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};
    
    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)r->swap_chain_extent.width;
    viewport.height = (float)r->swap_chain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = r->swap_chain_extent;
    
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Push constants
    VkPushConstantRange pushConstant = {};
    pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 16;  // vec4 * 4
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &r->descriptor_set_layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    
    if (vkCreatePipelineLayout(r->device, &pipelineLayoutInfo, nullptr, &r->pipeline_layout) != VK_SUCCESS) {
        return -1;
    }
    
    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = r->pipeline_layout;
    pipelineInfo.renderPass = r->render_pass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, 
                                   &r->graphics_pipeline) != VK_SUCCESS) {
        return -1;
    }
    
    // Cleanup shader modules
    vkDestroyShaderModule(r->device, vert_module, nullptr);
    vkDestroyShaderModule(r->device, frag_module, nullptr);
    
    return 0;
}

// ============================================================================
// Render
// ============================================================================

int vulkan_render(void* renderer, uint32_t image_index) {
    if (!renderer) return -1;
    
    auto* r = static_cast<VulkanRenderer*>(renderer);
    
    VkSemaphore waitSemaphores[] = {r->image_available_semaphores[r->current_frame]};
    VkSemaphore signalSemaphores[] = {r->render_finished_semaphores[r->current_frame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &r->command_buffers[image_index];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    if (vkQueueSubmit(r->graphics_queue, 1, &submitInfo, r->in_flight_fences[image_index]) != VK_SUCCESS) {
        return -1;
    }
    
    VkSemaphore presentSemaphores[] = {r->render_finished_semaphores[r->current_frame]};
    VkSwapchainKHR swapChains[] = {r->swap_chain};
    
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = presentSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &image_index;
    
    VkResult result = vkQueuePresentKHR(r->present_queue, &presentInfo);
    
    r->current_frame = (r->current_frame + 1) % r->swap_chain_image_views.size();
    
    return (result == VK_SUCCESS) ? 0 : -1;
}

int vulkan_wait_frame(void* renderer) {
    if (!renderer) return -1;
    
    auto* r = static_cast<VulkanRenderer*>(renderer);
    
    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(r->device, r->swap_chain, UINT64_MAX,
                                            r->image_available_semaphores[r->current_frame],
                                            VK_NULL_HANDLE, &image_index);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return 1;  // Recreate needed
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return -1;
    }
    
    // Wait for fence
    vkWaitForFences(r->device, 1, &r->in_flight_fences[image_index], VK_TRUE, UINT64_MAX);
    vkResetFences(r->device, 1, &r->in_flight_fences[image_index]);
    
    return (int)image_index;
}

// ============================================================================
// Texture Upload
// ============================================================================

int vulkan_upload_texture(void* renderer, const void* data, uint32_t width, uint32_t height) {
    if (!renderer || !data) return -1;
    
    auto* r = static_cast<VulkanRenderer*>(renderer);
    if (!r->device) return -1;
    
    // Create staging buffer
    VkDeviceSize image_size = width * height * 4;
    
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = image_size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(r->device, &bufferInfo, nullptr, &staging_buffer) != VK_SUCCESS) {
        return -1;
    }
    
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(r->device, staging_buffer, &mem_requirements);
    
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = mem_requirements.size;
    allocInfo.memoryTypeIndex = 0;  // Would find proper type
    
    if (vkAllocateMemory(r->device, &allocInfo, nullptr, &staging_memory) != VK_SUCCESS) {
        vkDestroyBuffer(r->device, staging_buffer, nullptr);
        return -1;
    }
    
    vkBindBufferMemory(r->device, staging_buffer, staging_memory, 0);
    
    // Copy data to staging buffer
    void* mapped;
    vkMapMemory(r->device, staging_memory, 0, image_size, 0, &mapped);
    memcpy(mapped, data, image_size);
    vkUnmapMemory(r->device, staging_memory);
    
    // Create image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    if (vkCreateImage(r->device, &imageInfo, nullptr, &r->texture_image) != VK_SUCCESS) {
        vkDestroyBuffer(r->device, staging_buffer, nullptr);
        vkFreeMemory(r->device, staging_memory, nullptr);
        return -1;
    }
    
    vkGetImageMemoryRequirements(r->device, r->texture_image, &mem_requirements);
    
    allocInfo.allocationSize = mem_requirements.size;
    if (vkAllocateMemory(r->device, &allocInfo, nullptr, &r->texture_memory) != VK_SUCCESS) {
        vkDestroyBuffer(r->device, staging_buffer, nullptr);
        vkFreeMemory(r->device, staging_memory, nullptr);
        return -1;
    }
    
    vkBindImageMemory(r->device, r->texture_image, r->texture_memory, 0);
    
    // Transition and copy (simplified - would use proper barriers)
    // vkCmdPipelineBarrier...
    
    vkDestroyBuffer(r->device, staging_buffer, nullptr);
    vkFreeMemory(r->device, staging_memory, nullptr);
    
    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = r->texture_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(r->device, &viewInfo, nullptr, &r->texture_image_view) != VK_SUCCESS) {
        return -1;
    }
    
    return 0;
}

// ============================================================================
// Result String
// ============================================================================

static const char* getVkResultString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "Success";
        case VK_NOT_READY: return "Not ready";
        case VK_TIMEOUT: return "Timeout";
        case VK_EVENT_SET: return "Event set";
        case VK_EVENT_RESET: return "Event reset";
        case VK_ERROR_OUT_OF_DATE_KHR: return "Out of date";
        case VK_SUBOPTIMAL_KHR: return "Suboptimal";
        default: return "Unknown";
    }
}

#ifdef __cplusplus
}
#endif
