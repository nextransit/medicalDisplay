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

/**
 * @brief Create compute pipeline for bit depth conversion
 * @param renderer Renderer handle
 * @param comp_shader Compute shader bytecode
 * @param comp_size Compute shader size in bytes
 * @return 0 on success
 */
int vulkan_create_compute_pipeline(void* renderer, const uint32_t* comp_shader, size_t comp_size);

/**
 * @brief Upload 16-bit texture data
 * @param renderer Renderer handle
 * @param data 16-bit pixel data
 * @param width Width in pixels
 * @param height Height in pixels
 * @return 0 on success
 */
int vulkan_upload_16bit_texture(void* renderer, const uint16_t* data, uint32_t width, uint32_t height);

/**
 * @brief Dispatch compute shader for bit depth conversion
 * @param renderer Renderer handle
 * @param width Width in pixels
 * @param height Height in pixels
 * @param windowCenter Window center value
 * @param windowWidth Window width value
 * @param bitsStored Bits stored in source
 * @param shift Right shift amount
 * @param modality Modality type
 * @param enableWindowLevel Enable window/level processing
 * @return 0 on success
 */
int vulkan_compute_dispatch(void* renderer, uint32_t width, uint32_t height,
                           float windowCenter, float windowWidth,
                           int bitsStored, int shift, int modality, int enableWindowLevel);

/**
 * @brief Read back 8-bit result from compute
 * @param renderer Renderer handle
 * @param output Output buffer for 8-bit pixels
 * @param width Width in pixels
 * @param height Height in pixels
 * @return 0 on success
 */
int vulkan_readback_8bit(void* renderer, uint8_t* output, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif // VULKAN_RENDERER_H
