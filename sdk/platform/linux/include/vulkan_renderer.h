/**
 * @file vulkan_renderer.h
 * @brief Vulkan Renderer Interface
 */

#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create Vulkan renderer
 * @param instance Vulkan instance
 * @param physical_device Physical device
 * @return Renderer handle
 */
void* vulkan_create(VkInstance instance, VkPhysicalDevice physical_device);

/**
 * @brief Destroy Vulkan renderer
 * @param renderer Renderer handle
 */
void vulkan_destroy(void* renderer);

/**
 * @brief Create graphics pipeline
 * @param renderer Renderer handle
 * @param vert_shader Vertex shader bytecode
 * @param vert_size Vertex shader size
 * @param frag_shader Fragment shader bytecode
 * @param frag_size Fragment shader size
 * @return 0 on success
 */
int vulkan_create_pipeline(void* renderer, const uint32_t* vert_shader, size_t vert_size,
                           const uint32_t* frag_shader, size_t frag_size);

/**
 * @brief Upload texture data
 * @param renderer Renderer handle
 * @param data Pixel data
 * @param width Width in pixels
 * @param height Height in pixels
 * @return 0 on success
 */
int vulkan_upload_texture(void* renderer, const void* data, uint32_t width, uint32_t height);

/**
 * @brief Render current frame
 * @param renderer Renderer handle
 * @param image_index Swap chain image index
 * @return 0 on success
 */
int vulkan_render(void* renderer, uint32_t image_index);

/**
 * @brief Wait for frame and get image index
 * @param renderer Renderer handle
 * @return Image index, negative on error, 1 if recreate needed
 */
int vulkan_wait_frame(void* renderer);

#ifdef __cplusplus
}
#endif

#endif // VULKAN_RENDERER_H
