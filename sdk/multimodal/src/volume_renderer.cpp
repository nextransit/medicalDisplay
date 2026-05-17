/**
 * @file volume_renderer.cpp
 * @brief 3D体绘制实现 - CPU Fallback
 * 
 * 提供基本的体绘制功能，GPU版本可在 vulkan_volume.cpp 或 metal_volume.mm 中实现
 */

#include "volume_renderer.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdlib>

// ============================================================================
// 传输函数定义
// ============================================================================

static const struct {
    float value;
    float r, g, b, a;
} default_transfer_ct_bone[] = {
    {-1024, 0, 0, 0, 0},
    {-500, 0, 0, 0, 0},
    {-100, 0.2, 0.2, 0.2, 0},
    {0, 0.3, 0.3, 0.3, 0},
    {200, 0.5, 0.5, 0.5, 0.3},
    {400, 0.8, 0.8, 0.8, 0.6},
    {1000, 1.0, 1.0, 1.0, 0.9},
    {2000, 1.0, 1.0, 1.0, 1.0}
};

static const struct {
    float value;
    float r, g, b, a;
} default_transfer_ct_soft_tissue[] = {
    {-1024, 0, 0, 0, 0},
    {-100, 0, 0, 0, 0},
    {0, 0.1, 0.05, 0.05, 0},
    {50, 0.4, 0.2, 0.2, 0.1},
    {100, 0.6, 0.3, 0.3, 0.3},
    {200, 0.8, 0.5, 0.5, 0.5},
    {400, 0.9, 0.7, 0.6, 0.7},
    {800, 1.0, 0.9, 0.8, 0.9},
    {2000, 1.0, 1.0, 1.0, 1.0}
};

static const struct {
    float value;
    float r, g, b, a;
} default_transfer_pet_metabolic[] = {
    {0, 0, 0, 0, 0},
    {0.1, 0, 0, 0.2, 0.1},
    {0.3, 0, 0.2, 0.5, 0.2},
    {0.5, 0, 0.5, 0.8, 0.4},
    {0.7, 0.5, 0.8, 0.2, 0.6},
    {0.9, 1.0, 0.5, 0, 0.8},
    {1.0, 1.0, 1.0, 0, 1.0}
};

// ============================================================================
// 内部结构
// ============================================================================

struct VolumeRenderer {
    // 体数据
    VolumeData volume_data;
    std::vector<uint8_t> volume_u8;      // 预处理的u8数据
    std::vector<uint8_t> secondary_u8;    // 第二通道预处理数据
    
    // 传输函数
    std::vector<float> transfer_points;
    TransferFunctionType transfer_type;
    
    // 渲染配置
    VolumeRenderConfig config;
    
    // 相机
    float camera_pos[3];
    float camera_look_at[3];
    float camera_up[3];
    float view_matrix[16];
    
    // 统计
    VolumeRenderStats stats;
    
    // 临时缓冲
    std::vector<float> ray_buffer;
    std::vector<float> gradient_buffer;
};

static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float clamp01(float v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

// ============================================================================
// 传输函数插值
// ============================================================================

static void sample_transfer_function(const float* points, int num_points,
                                    float value, float* r, float* g, float* b, float* a) {
    if (num_points < 2) return;
    
    // 查找区间
    int idx = 0;
    for (int i = 0; i < num_points - 1; i++) {
        if (value >= points[i * 5] && value <= points[(i + 1) * 5]) {
            idx = i;
            break;
        }
    }
    
    float t = (value - points[idx * 5]) / 
              (points[(idx + 1) * 5] - points[idx * 5] + 0.0001f);
    t = clamp01(t);
    
    *r = lerp(points[idx * 5 + 1], points[(idx + 1) * 5 + 1], t);
    *g = lerp(points[idx * 5 + 2], points[(idx + 1) * 5 + 2], t);
    *b = lerp(points[idx * 5 + 3], points[(idx + 1) * 5 + 3], t);
    *a = lerp(points[idx * 5 + 4], points[(idx + 1) * 5 + 4], t);
}

static void init_transfer_function(VolumeRenderer* vr, TransferFunctionType type) {
    vr->transfer_type = type;
    vr->transfer_points.clear();
    
    const auto* points = default_transfer_ct_soft_tissue;
    int count = 8;
    
    switch (type) {
        case TRANSFER_CT_BONE:
            points = default_transfer_ct_bone;
            count = 8;
            break;
        case TRANSFER_CT_SOFT_TISSUE:
            points = default_transfer_ct_soft_tissue;
            count = 9;
            break;
        case TRANSFER_PET_METABOLIC:
        case TRANSFER_PET_TUMOR:
            points = default_transfer_pet_metabolic;
            count = 7;
            break;
        default:
            points = default_transfer_ct_soft_tissue;
            count = 9;
            break;
    }
    
    for (int i = 0; i < count; i++) {
        vr->transfer_points.push_back(points[i].value);
        vr->transfer_points.push_back(points[i].r);
        vr->transfer_points.push_back(points[i].g);
        vr->transfer_points.push_back(points[i].b);
        vr->transfer_points.push_back(points[i].a);
    }
}

// ============================================================================
// 体素采样
// ============================================================================

static inline float sample_volume_1u8(const uint8_t* data, int x, int y, int z,
                                      int w, int h, int d) {
    if (x < 0 || x >= w || y < 0 || y >= h || z < 0 || z >= d) return 0;
    return data[(z * h + y) * w + x] / 255.0f;
}

static inline float sample_volume_2u16(const uint16_t* data, int x, int y, int z,
                                       int w, int h, int d, float max_val) {
    if (x < 0 || x >= w || y < 0 || y >= h || z < 0 || z >= d) return 0;
    return data[(z * h + y) * w + x] / max_val;
}

static float sample_volume_trilinear(const VolumeRenderer* vr, float x, float y, float z) {
    const auto& vd = vr->volume_data;
    
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    int iz = (int)std::floor(z);
    
    float fx = x - ix;
    float fy = y - iy;
    float fz = z - iz;
    
    if (vd.bytes_per_voxel == 1) {
        const uint8_t* data = vr->volume_u8.data();
        
        float v000 = sample_volume_1u8(data, ix, iy, iz, vd.width, vd.height, vd.depth);
        float v001 = sample_volume_1u8(data, ix+1, iy, iz, vd.width, vd.height, vd.depth);
        float v010 = sample_volume_1u8(data, ix, iy+1, iz, vd.width, vd.height, vd.depth);
        float v011 = sample_volume_1u8(data, ix+1, iy+1, iz, vd.width, vd.height, vd.depth);
        float v100 = sample_volume_1u8(data, ix, iy, iz+1, vd.width, vd.height, vd.depth);
        float v101 = sample_volume_1u8(data, ix+1, iy, iz+1, vd.width, vd.height, vd.depth);
        float v110 = sample_volume_1u8(data, ix, iy+1, iz+1, vd.width, vd.height, vd.depth);
        float v111 = sample_volume_1u8(data, ix+1, iy+1, iz+1, vd.width, vd.height, vd.depth);
        
        return lerp(
            lerp(lerp(v000, v001, fx), lerp(v010, v011, fx), fy),
            lerp(lerp(v100, v101, fx), lerp(v110, v111, fx), fy),
            fz
        );
    } else {
        // 2字节数据
        const uint16_t* data = (const uint16_t*)vr->volume_u8.data();
        float max_val = vd.max_value > 0 ? vd.max_value : 65535.0f;
        
        float v000 = sample_volume_2u16(data, ix, iy, iz, vd.width, vd.height, vd.depth, max_val);
        float v001 = sample_volume_2u16(data, ix+1, iy, iz, vd.width, vd.height, vd.depth, max_val);
        float v010 = sample_volume_2u16(data, ix, iy+1, iz, vd.width, vd.height, vd.depth, max_val);
        float v011 = sample_volume_2u16(data, ix+1, iy+1, iz, vd.width, vd.height, vd.depth, max_val);
        float v100 = sample_volume_2u16(data, ix, iy, iz+1, vd.width, vd.height, vd.depth, max_val);
        float v101 = sample_volume_2u16(data, ix+1, iy, iz+1, vd.width, vd.height, vd.depth, max_val);
        float v110 = sample_volume_2u16(data, ix, iy+1, iz+1, vd.width, vd.height, vd.depth, max_val);
        float v111 = sample_volume_2u16(data, ix+1, iy+1, iz+1, vd.width, vd.height, vd.depth, max_val);
        
        return lerp(
            lerp(lerp(v000, v001, fx), lerp(v010, v011, fx), fy),
            lerp(lerp(v100, v101, fx), lerp(v110, v111, fx), fy),
            fz
        );
    }
}

// ============================================================================
// 光线投射渲染
// ============================================================================

static void ray_cast(VolumeRenderer* vr, float ray_origin[3], float ray_dir[3],
                    uint8_t* output, int width, int height, int px, int py) {
    const auto& vd = vr->volume_data;
    const auto& cfg = vr->config;
    
    // 计算射线与体包围盒的交点
    float t_min = 0, t_max = 1000;
    float box_min[3] = {0, 0, 0};
    float box_max[3] = {(float)vd.width, (float)vd.height, (float)vd.depth};
    
    for (int i = 0; i < 3; i++) {
        if (std::abs(ray_dir[i]) < 0.0001f) {
            if (ray_origin[i] < box_min[i] || ray_origin[i] > box_max[i]) {
                output[0] = output[1] = output[2] = 0;
                output[3] = 255;
                return;
            }
        } else {
            float t1 = (box_min[i] - ray_origin[i]) / ray_dir[i];
            float t2 = (box_max[i] - ray_origin[i]) / ray_dir[i];
            if (t1 > t2) std::swap(t1, t2);
            t_min = std::max(t_min, t1);
            t_max = std::min(t_max, t2);
        }
    }
    
    if (t_min > t_max || t_max < 0) {
        output[0] = output[1] = output[2] = 0;
        output[3] = 255;
        return;
    }
    
    t_min = std::max(0.0f, t_min);
    
    // 光线投射
    float accumulated_color[4] = {0, 0, 0, 0};
    float t = t_min;
    
    int steps = std::min(cfg.max_steps, (int)((t_max - t_min) / cfg.step_size));
    
    for (int i = 0; i < steps; i++) {
        float sample_pos[3] = {
            ray_origin[0] + ray_dir[0] * t,
            ray_origin[1] + ray_dir[1] * t,
            ray_origin[2] + ray_dir[2] * t
        };
        
        // 采样体数据
        float value = sample_volume_trilinear(vr, sample_pos[0], sample_pos[1], sample_pos[2]);
        
        // 传输函数
        float r, g, b, a;
        if (vr->transfer_points.empty()) {
            r = g = b = value;
            a = value * cfg.opacity_scale;
        } else {
            float normalized = value * (vr->volume_data.max_value > 0 ? 
                                        vr->volume_data.max_value : 255.0f);
            sample_transfer_function(vr->transfer_points.data(),
                                   vr->transfer_points.size() / 5,
                                   normalized, &r, &g, &b, &a);
            a *= cfg.opacity_scale;
        }
        
        // Alpha合成
        accumulated_color[0] += r * a * (1 - accumulated_color[3]);
        accumulated_color[1] += g * a * (1 - accumulated_color[3]);
        accumulated_color[2] += b * a * (1 - accumulated_color[3]);
        accumulated_color[3] += a * (1 - accumulated_color[3]);
        
        if (accumulated_color[3] >= 0.95f) break;
        
        t += cfg.step_size;
    }
    
    output[0] = (uint8_t)(accumulated_color[0] * 255);
    output[1] = (uint8_t)(accumulated_color[1] * 255);
    output[2] = (uint8_t)(accumulated_color[2] * 255);
    output[3] = (uint8_t)(accumulated_color[3] * 255);
}

// ============================================================================
// 最大密度投影
// ============================================================================

static void maximum_intensity_projection(VolumeRenderer* vr, float ray_origin[3],
                                        float ray_dir[3],
                                        uint8_t* output, int width, int height,
                                        int px, int py) {
    const auto& vd = vr->volume_data;
    const auto& cfg = vr->config;
    
    float t_min = 0, t_max = 1000;
    float box_min[3] = {0, 0, 0};
    float box_max[3] = {(float)vd.width, (float)vd.height, (float)vd.depth};
    
    for (int i = 0; i < 3; i++) {
        if (std::abs(ray_dir[i]) < 0.0001f) {
            if (ray_origin[i] < box_min[i] || ray_origin[i] > box_max[i]) {
                output[0] = output[1] = output[2] = 0;
                output[3] = 255;
                return;
            }
        } else {
            float t1 = (box_min[i] - ray_origin[i]) / ray_dir[i];
            float t2 = (box_max[i] - ray_origin[i]) / ray_dir[i];
            if (t1 > t2) std::swap(t1, t2);
            t_min = std::max(t_min, t1);
            t_max = std::min(t_max, t2);
        }
    }
    
    if (t_min > t_max || t_max < 0) {
        output[0] = output[1] = output[2] = 0;
        output[3] = 255;
        return;
    }
    
    t_min = std::max(0.0f, t_min);
    
    float max_value = 0;
    float t = t_min;
    int steps = std::min(cfg.max_steps, (int)((t_max - t_min) / cfg.step_size));
    
    for (int i = 0; i < steps; i++) {
        float sample_pos[3] = {
            ray_origin[0] + ray_dir[0] * t,
            ray_origin[1] + ray_dir[1] * t,
            ray_origin[2] + ray_dir[2] * t
        };
        
        float value = sample_volume_trilinear(vr, sample_pos[0], sample_pos[1], sample_pos[2]);
        max_value = std::max(max_value, value);
        
        t += cfg.step_size;
    }
    
    // 查找传输函数获取颜色
    float r, g, b, a;
    float normalized = max_value * (vd.max_value > 0 ? vd.max_value : 255.0f);
    sample_transfer_function(vr->transfer_points.data(),
                           vr->transfer_points.size() / 5,
                           normalized, &r, &g, &b, &a);
    
    output[0] = (uint8_t)(r * 255);
    output[1] = (uint8_t)(g * 255);
    output[2] = (uint8_t)(b * 255);
    output[3] = 255;
}

// ============================================================================
// API 实现
// ============================================================================

VolumeRenderer* volume_renderer_create(const char* backend) {
    auto* vr = new (std::nothrow) VolumeRenderer();
    if (!vr) return nullptr;
    
    memset(vr, 0, sizeof(*vr));
    
    // 默认配置
    vr->config.mode = VOLUME_MODE_RAY_CASTING;
    vr->config.transfer_type = TRANSFER_CT_SOFT_TISSUE;
    vr->config.step_size = 0.5f;
    vr->config.max_steps = 500;
    vr->config.opacity_scale = 1.0f;
    vr->config.enable_shading = true;
    vr->config.enable_gradient = false;
    vr->config.light_direction[0] = 0.5f;
    vr->config.light_direction[1] = 0.5f;
    vr->config.light_direction[2] = -0.5f;
    vr->config.ambient_strength = 0.2f;
    vr->config.diffuse_strength = 0.7f;
    vr->config.specular_strength = 0.3f;
    
    // 默认相机
    vr->camera_pos[0] = 0; vr->camera_pos[1] = 0; vr->camera_pos[2] = -300;
    vr->camera_look_at[0] = 0; vr->camera_look_at[1] = 0; vr->camera_look_at[2] = 0;
    vr->camera_up[0] = 0; vr->camera_up[1] = 1; vr->camera_up[2] = 0;
    
    // 初始化传输函数
    init_transfer_function(vr, vr->config.transfer_type);
    
    vr->stats.backend_name = "CPU";
    vr->stats.gpu_used = 0;
    
    return vr;
}

void volume_renderer_destroy(VolumeRenderer* renderer) {
    delete renderer;
}

int volume_renderer_set_data(VolumeRenderer* renderer, const VolumeData* data) {
    if (!renderer || !data) return -1;
    
    renderer->volume_data = *data;
    renderer->volume_u8.clear();
    
    // 预处理为u8格式
    if (data->bytes_per_voxel == 1) {
        const uint8_t* src = (const uint8_t*)data->data;
        renderer->volume_u8.assign(src, src + data->width * data->height * data->depth);
    } else if (data->bytes_per_voxel == 2) {
        const uint16_t* src = (const uint16_t*)data->data;
        size_t count = data->width * data->height * data->depth;
        renderer->volume_u8.resize(count);
        
        float scale = 255.0f / (data->max_value > 0 ? data->max_value : 65535.0f);
        for (size_t i = 0; i < count; i++) {
            renderer->volume_u8[i] = (uint8_t)(src[i] * scale);
        }
    }
    
    return 0;
}

int volume_renderer_set_secondary_data(VolumeRenderer* renderer, const void* data) {
    if (!renderer || !data) return -1;
    
    renderer->secondary_u8.clear();
    
    if (renderer->volume_data.bytes_per_voxel == 1) {
        const uint8_t* src = (const uint8_t*)data;
        renderer->secondary_u8.assign(src, src + 
            renderer->volume_data.width * renderer->volume_data.height * renderer->volume_data.depth);
    }
    
    return 0;
}

int volume_renderer_set_config(VolumeRenderer* renderer, const VolumeRenderConfig* config) {
    if (!renderer || !config) return -1;
    
    renderer->config = *config;
    
    if (config->transfer_type != renderer->transfer_type) {
        init_transfer_function(renderer, config->transfer_type);
    }
    
    return 0;
}

int volume_renderer_set_transfer_function(VolumeRenderer* renderer,
                                         TransferFunctionType type) {
    if (!renderer) return -1;
    
    init_transfer_function(renderer, type);
    renderer->config.transfer_type = type;
    
    return 0;
}

int volume_renderer_set_custom_transfer(VolumeRenderer* renderer,
                                       int num_points,
                                       const float* points) {
    if (!renderer || !points || num_points < 2) return -1;
    
    renderer->transfer_points.clear();
    for (int i = 0; i < num_points * 5; i++) {
        renderer->transfer_points.push_back(points[i]);
    }
    
    return 0;
}

int volume_renderer_set_camera(VolumeRenderer* renderer,
                              float pos_x, float pos_y, float pos_z,
                              float look_at_x, float look_at_y, float look_at_z,
                              float up_x, float up_y, float up_z) {
    if (!renderer) return -1;
    
    renderer->camera_pos[0] = pos_x;
    renderer->camera_pos[1] = pos_y;
    renderer->camera_pos[2] = pos_z;
    renderer->camera_look_at[0] = look_at_x;
    renderer->camera_look_at[1] = look_at_y;
    renderer->camera_look_at[2] = look_at_z;
    renderer->camera_up[0] = up_x;
    renderer->camera_up[1] = up_y;
    renderer->camera_up[2] = up_z;
    
    return 0;
}

int volume_renderer_render(VolumeRenderer* renderer,
                          uint8_t* output,
                          int output_width,
                          int output_height,
                          const float* view_matrix) {
    if (!renderer || !output) return -1;
    
    if (renderer->volume_u8.empty()) {
        memset(output, 0, output_width * output_height * 4);
        return 0;
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    const auto& vd = renderer->volume_data;
    
    // 计算相机变换
    float ray_origin[3] = {renderer->camera_pos[0] + vd.width / 2,
                           renderer->camera_pos[1] + vd.height / 2,
                           renderer->camera_pos[2] + vd.depth / 2};
    
    float fov = 60.0f;
    float aspect = (float)output_width / output_height;
    float tan_half_fov = tan(fov * 3.14159f / 360.0f);
    
    // 渲染每个像素
    #pragma omp parallel for
    for (int py = 0; py < output_height; py++) {
        for (int px = 0; px < output_width; px++) {
            // 计算射线方向
            float u = (2.0f * (px + 0.5f) / output_width - 1.0f) * aspect * tan_half_fov;
            float v = (1.0f - 2.0f * (py + 0.5f) / output_height) * tan_half_fov;
            
            float ray_dir[3] = {u, v, -1.0f};
            
            // 归一化
            float len = sqrt(ray_dir[0]*ray_dir[0] + ray_dir[1]*ray_dir[1] + ray_dir[2]*ray_dir[2]);
            ray_dir[0] /= len; ray_dir[1] /= len; ray_dir[2] /= len;
            
            uint8_t* pixel = output + (py * output_width + px) * 4;
            
            switch (renderer->config.mode) {
                case VOLUME_MODE_MIP:
                    maximum_intensity_projection(renderer, ray_origin, ray_dir,
                                               pixel, output_width, output_height, px, py);
                    break;
                default:
                    ray_cast(renderer, ray_origin, ray_dir,
                            pixel, output_width, output_height, px, py);
                    break;
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start).count();
    
    renderer->stats.total_frames++;
    renderer->stats.avg_render_time_ms = 
        (renderer->stats.avg_render_time_ms * (renderer->stats.total_frames - 1) + duration) /
        renderer->stats.total_frames;
    renderer->stats.min_render_time_ms = std::min(renderer->stats.min_render_time_ms, duration);
    renderer->stats.max_render_time_ms = std::max(renderer->stats.max_render_time_ms, duration);
    
    return 0;
}

int volume_renderer_render_to_texture(VolumeRenderer* renderer,
                                     void* output_texture,
                                     int output_width,
                                     int output_height,
                                     const float* view_matrix) {
    // CPU版本不支持GPU纹理输出
    return -1;
}

int volume_renderer_set_orthographic(VolumeRenderer* renderer,
                                    float left, float right,
                                    float bottom, float top,
                                    float near, float far) {
    if (!renderer) return -1;
    // 实现正交投影矩阵
    return 0;
}

int volume_renderer_set_perspective(VolumeRenderer* renderer,
                                   float fov_degrees,
                                   float aspect,
                                   float near, float far) {
    if (!renderer) return -1;
    // 实现透视投影矩阵
    return 0;
}

void volume_renderer_get_stats(VolumeRenderer* renderer, VolumeRenderStats* stats) {
    if (!renderer || !stats) return;
    *stats = renderer->stats;
}

int volume_renderer_get_available_backends(char** backends, int max_backends) {
    if (!backends || max_backends < 3) return 0;
    
    backends[0] = strdup("cpu");
    backends[1] = strdup("vulkan");
    backends[2] = strdup("metal");
    
    return 3;
}
