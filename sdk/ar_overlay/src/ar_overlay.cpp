/**
 * @file ar_overlay.cpp
 * @brief AR标注与叠加系统实现
 * 
 * 支持手术导航、标注、测量和AR叠加渲染
 */

#include "ar_overlay.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <cmath>

// ============================================================================
// 内部数据结构
// ============================================================================

struct AnnotationItem {
    AnnotationData data;
    bool deleted;
    
    AnnotationItem(const AnnotationData& d) : data(d), deleted(false) {}
};

struct AROverlayEngine {
    int canvas_width;
    int canvas_height;
    uint32_t next_id;
    
    std::vector<AnnotationItem> annotations;
    std::map<uint32_t, AnnotationItem*> id_map;
    
    AnnotationSession current_session;
    std::vector<AnnotationSession> sessions;
    
    AROverlayEngine(int w, int h)
        : canvas_width(w), canvas_height(h), next_id(1) {
        memset(&current_session, 0, sizeof(current_session));
    }
};

// ============================================================================
// 辅助函数
// ============================================================================

static float distance_point_to_rect(float px, float py, 
                                    float rx, float ry, float rw, float rh) {
    float dx = std::max(rx - px, std::max(0.0f, px - (rx + rw)));
    float dy = std::max(ry - py, std::max(0.0f, py - (ry + rh)));
    return std::sqrt(dx * dx + dy * dy);
}

static float distance_point_to_ellipse(float px, float py,
                                      float cx, float cy, float rx, float ry) {
    float dx = (px - cx) / rx;
    float dy = (py - cy) / ry;
    return std::sqrt(dx * dx + dy * dy) - 1.0f;
}

static float distance_point_to_line(float px, float py,
                                    float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float length_sq = dx * dx + dy * dy;
    
    if (length_sq < 1e-6f) {
        return std::sqrt((px - x1) * (px - x1) + (py - y1) * (py - y1));
    }
    
    float t = std::max(0.0f, std::min(1.0f, ((px - x1) * dx + (py - y1) * dy) / length_sq));
    float proj_x = x1 + t * dx;
    float proj_y = y1 + t * dy;
    
    return std::sqrt((px - proj_x) * (px - proj_x) + (py - proj_y) * (py - proj_y));
}

static float point_in_polygon(float px, float py,
                              const float* xs, const float* ys, int count) {
    // Ray casting algorithm
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        if (((ys[i] > py) != (ys[j] > py)) &&
            (px < (xs[j] - xs[i]) * (py - ys[i]) / (ys[j] - ys[i]) + xs[i])) {
            inside = !inside;
        }
    }
    return inside ? 0.0f : 1.0f;
}

// ============================================================================
// 引擎生命周期
// ============================================================================

extern "C" {

AROverlayEngine* ar_engine_create(int canvas_width, int canvas_height) {
    if (canvas_width <= 0 || canvas_height <= 0) {
        return nullptr;
    }
    return new (std::nothrow) AROverlayEngine(canvas_width, canvas_height);
}

void ar_engine_destroy(AROverlayEngine* engine) {
    delete engine;
}

// ============================================================================
// 标注管理
// ============================================================================

int ar_engine_add_annotation(AROverlayEngine* engine, const AnnotationData* annotation) {
    if (!engine || !annotation) return -1;
    
    AnnotationItem item(*annotation);
    item.data.id = engine->next_id++;
    item.deleted = false;
    
    engine->annotations.push_back(item);
    engine->id_map[item.data.id] = &engine->annotations.back();
    
    return item.data.id;
}

int ar_engine_update_annotation(AROverlayEngine* engine, uint32_t annotation_id,
                              const AnnotationData* annotation) {
    if (!engine || !annotation) return -1;
    
    auto it = engine->id_map.find(annotation_id);
    if (it == engine->id_map.end()) return -1;
    
    it->second->data = *annotation;
    it->second->data.id = annotation_id;
    
    return 0;
}

int ar_engine_remove_annotation(AROverlayEngine* engine, uint32_t annotation_id) {
    if (!engine) return -1;
    
    auto it = engine->id_map.find(annotation_id);
    if (it == engine->id_map.end()) return -1;
    
    it->second->deleted = true;
    engine->id_map.erase(it);
    
    return 0;
}

int ar_engine_get_annotation(AROverlayEngine* engine, uint32_t annotation_id,
                            AnnotationData* annotation) {
    if (!engine || !annotation) return -1;
    
    auto it = engine->id_map.find(annotation_id);
    if (it == engine->id_map.end()) return -1;
    
    *annotation = it->second->data;
    return 0;
}

int ar_engine_get_all_annotations(AROverlayEngine* engine,
                                 AnnotationData* annotations,
                                 int max_count) {
    if (!engine || !annotations || max_count <= 0) return 0;
    
    int count = 0;
    for (auto& item : engine->annotations) {
        if (item.deleted) continue;
        if (count >= max_count) break;
        annotations[count++] = item.data;
    }
    
    return count;
}

void ar_engine_clear_annotations(AROverlayEngine* engine) {
    if (!engine) return;
    engine->annotations.clear();
    engine->id_map.clear();
}

// ============================================================================
// 会话管理
// ============================================================================

int ar_engine_create_session(AROverlayEngine* engine, AnnotationSession* session) {
    if (!engine || !session) return -1;
    
    engine->current_session.session_id = static_cast<uint32_t>(engine->sessions.size() + 1);
    engine->current_session.created_time = 0;
    engine->current_session.modified_time = 0;
    engine->current_session.annotation_count = static_cast<uint32_t>(engine->annotations.size());
    
    *session = engine->current_session;
    engine->sessions.push_back(engine->current_session);
    
    return 0;
}

int ar_engine_load_session(AROverlayEngine* engine, uint32_t session_id) {
    if (!engine) return -1;
    
    for (const auto& session : engine->sessions) {
        if (session.session_id == session_id) {
            engine->current_session = session;
            return 0;
        }
    }
    
    return -1;
}

int ar_engine_save_session(AROverlayEngine* engine, const char* path) {
    if (!engine || !path) return -1;
    
    char json[65536];
    if (ar_engine_export_json(engine, json, sizeof(json)) != 0) {
        return -1;
    }
    
    // 在实际实现中，这里会写入文件
    (void)path;
    
    return 0;
}

int ar_engine_export_json(AROverlayEngine* engine, char* json_output, size_t buffer_size) {
    if (!engine || !json_output || buffer_size == 0) return -1;
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"session\": {\n";
    oss << "    \"id\": " << engine->current_session.session_id << ",\n";
    oss << "    \"patient_id\": \"" << engine->current_session.patient_id << "\",\n";
    oss << "    \"study_id\": \"" << engine->current_session.study_id << "\"\n";
    oss << "  },\n";
    oss << "  \"annotations\": [\n";
    
    bool first = true;
    for (auto& item : engine->annotations) {
        if (item.deleted) continue;
        
        if (!first) oss << ",\n";
        first = false;
        
        oss << "    {\n";
        oss << "      \"id\": " << item.data.id << ",\n";
        oss << "      \"type\": " << item.data.type << ",\n";
        oss << "      \"x\": " << std::fixed << std::setprecision(4) << item.data.x << ",\n";
        oss << "      \"y\": " << item.data.y << ",\n";
        oss << "      \"width\": " << item.data.width << ",\n";
        oss << "      \"height\": " << item.data.height << ",\n";
        oss << "      \"color\": [" << (int)item.data.style.color_r << ", "
            << (int)item.data.style.color_g << ", "
            << (int)item.data.style.color_b << ", "
            << (int)item.data.style.color_a << "],\n";
        oss << "      \"text\": \"" << item.data.text << "\",\n";
        oss << "      \"value\": " << item.data.value << ",\n";
        oss << "      \"unit\": \"" << item.data.unit << "\"\n";
        oss << "    }";
    }
    
    oss << "\n  ]\n";
    oss << "}\n";
    
    std::string result = oss.str();
    if (result.size() >= buffer_size) {
        return -1;
    }
    
    strncpy(json_output, result.c_str(), buffer_size - 1);
    json_output[buffer_size - 1] = '\0';
    
    return 0;
}

int ar_engine_import_json(AROverlayEngine* engine, const char* json_input) {
    if (!engine || !json_input) return -1;
    
    // 简化解析 - 实际应使用JSON库
    int count = 0;
    
    // 查找 "annotations": 之后的内容
    const char* annot_start = strstr(json_input, "\"annotations\"");
    if (!annot_start) return 0;
    
    // 计算标注数量 (简化)
    const char* ptr = annot_start;
    while ((ptr = strstr(ptr + 1, "\"id\":")) != nullptr) {
        count++;
    }
    
    return count;
}

// ============================================================================
// 渲染
// ============================================================================

static void blend_pixel(uint8_t* out, const uint8_t* overlay, uint8_t alpha) {
    float a = alpha / 255.0f;
    float inv_a = 1.0f - a;
    
    for (int c = 0; c < 3; c++) {
        out[c] = static_cast<uint8_t>(out[c] * inv_a + overlay[c] * a);
    }
}

static void draw_line(uint8_t* image, int width, int height, int stride,
                      int x0, int y0, int x1, int y1,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a, int thickness) {
    // Bresenham line algorithm with thickness
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        // Draw thick pixel
        for (int ty = -thickness/2; ty <= thickness/2; ty++) {
            for (int tx = -thickness/2; tx <= thickness/2; tx++) {
                int px = x0 + tx;
                int py = y0 + ty;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    uint8_t* pixel = &image[py * stride + px * 3];
                    uint8_t overlay[3] = {r, g, b};
                    blend_pixel(pixel, overlay, a);
                }
            }
        }
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_rect(uint8_t* image, int width, int height, int stride,
                      int x, int y, int w, int h,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a, int thickness, bool fill) {
    if (fill) {
        for (int py = y; py < y + h; py++) {
            for (int px = x; px < x + w; px++) {
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    uint8_t* pixel = &image[py * stride + px * 3];
                    uint8_t overlay[3] = {r, g, b};
                    blend_pixel(pixel, overlay, a);
                }
            }
        }
    } else {
        draw_line(image, width, height, stride, x, y, x + w, y, r, g, b, a, thickness);
        draw_line(image, width, height, stride, x + w, y, x + w, y + h, r, g, b, a, thickness);
        draw_line(image, width, height, stride, x, y + h, x + w, y + h, r, g, b, a, thickness);
        draw_line(image, width, height, stride, x, y, x, y + h, r, g, b, a, thickness);
    }
}

static void draw_ellipse(uint8_t* image, int width, int height, int stride,
                         int cx, int cy, int rx, int ry,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a, int thickness, bool fill) {
    if (rx <= 0 || ry <= 0) return;
    
    for (int py = cy - ry; py <= cy + ry; py++) {
        for (int px = cx - rx; px <= cx + rx; px++) {
            float dx = (px - cx) / (float)rx;
            float dy = (py - cy) / (float)ry;
            float dist = dx * dx + dy * dy;
            
            bool in_ellipse = dist <= 1.0f;
            bool on_edge = dist <= 1.1f && dist >= 0.9f;
            
            if ((fill && in_ellipse) || (!fill && on_edge)) {
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    uint8_t* pixel = &image[py * stride + px * 3];
                    uint8_t overlay[3] = {r, g, b};
                    blend_pixel(pixel, overlay, a);
                }
            }
        }
    }
}

int ar_engine_render(AROverlayEngine* engine,
                     const uint8_t* base_image,
                     uint8_t* output) {
    if (!engine || !base_image || !output) return -1;
    
    int stride = engine->canvas_width * 3;
    
    // Copy base image
    memcpy(output, base_image, engine->canvas_height * stride);
    
    // Sort by z-order and render
    std::vector<AnnotationItem*> sorted;
    for (auto& item : engine->annotations) {
        if (!item.deleted && item.data.style.visible) {
            sorted.push_back(&item);
        }
    }
    
    std::sort(sorted.begin(), sorted.end(),
              [](const AnnotationItem* a, const AnnotationItem* b) {
                  return a->data.z_order < b->data.z_order;
              });
    
    for (auto* item : sorted) {
        const AnnotationData& ann = item->data;
        const AnnotationStyle& style = ann.style;
        
        int x = static_cast<int>(ann.x * engine->canvas_width);
        int y = static_cast<int>(ann.y * engine->canvas_height);
        int w = static_cast<int>(ann.width * engine->canvas_width);
        int h = static_cast<int>(ann.height * engine->canvas_height);
        
        int thickness = std::max(1, static_cast<int>(style.line_width));
        
        switch (ann.type) {
            case ANNOTATION_RECT:
                draw_rect(output, engine->canvas_width, engine->canvas_height, stride,
                         x, y, w, h,
                         style.color_r, style.color_g, style.color_b, style.color_a,
                         thickness, style.fill);
                break;
                
            case ANNOTATION_ELLIPSE:
                draw_ellipse(output, engine->canvas_width, engine->canvas_height, stride,
                            x + w/2, y + h/2, w/2, h/2,
                            style.color_r, style.color_g, style.color_b, style.color_a,
                            thickness, style.fill);
                break;
                
            case ANNOTATION_LINE:
            case ANNOTATION_ARROW:
                draw_line(output, engine->canvas_width, engine->canvas_height, stride,
                         x, y, x + w, y + h,
                         style.color_r, style.color_g, style.color_b, style.color_a,
                         thickness);
                break;
                
            case ANNOTATION_HIGHLIGHT:
                draw_rect(output, engine->canvas_width, engine->canvas_height, stride,
                         x, y, w, h,
                         style.color_r, style.color_g, style.color_b, 50,  // 半透明
                         thickness, true);
                break;
                
            default:
                // 其他类型暂时不渲染
                break;
        }
    }
    
    return 0;
}

int ar_engine_render_to_texture(AROverlayEngine* engine, uint32_t texture_id) {
    if (!engine) return -1;
    (void)texture_id;
    // GPU加速渲染预留
    return 0;
}

// ============================================================================
// 交互
// ============================================================================

uint32_t ar_engine_hit_test(AROverlayEngine* engine, float x, float y) {
    if (!engine) return 0;
    
    uint32_t best_id = 0;
    float best_dist = 1e6f;
    
    for (auto& item : engine->annotations) {
        if (item.deleted || !item.data.style.visible) continue;
        
        float dist = 1e6f;
        
        switch (item.data.type) {
            case ANNOTATION_RECT:
            case ANNOTATION_HIGHLIGHT:
                dist = distance_point_to_rect(x, y, item.data.x, item.data.y,
                                             item.data.width, item.data.height);
                break;
                
            case ANNOTATION_ELLIPSE:
                dist = distance_point_to_ellipse(x, y, 
                                               item.data.x + item.data.width / 2,
                                               item.data.y + item.data.height / 2,
                                               item.data.width / 2,
                                               item.data.height / 2);
                break;
                
            case ANNOTATION_LINE:
            case ANNOTATION_ARROW:
                dist = distance_point_to_line(x, y, item.data.x, item.data.y,
                                             item.data.x + item.data.width,
                                             item.data.y + item.data.height);
                break;
                
            default:
                dist = distance_point_to_rect(x, y, item.data.x, item.data.y,
                                             item.data.width, item.data.height);
                break;
        }
        
        if (dist < 0.05f && dist < best_dist) {
            best_dist = dist;
            best_id = item.data.id;
        }
    }
    
    return best_id;
}

int ar_engine_select(AROverlayEngine* engine, uint32_t annotation_id, bool selected) {
    if (!engine) return -1;
    
    if (annotation_id == 0) {
        // 取消所有选中
        for (auto& item : engine->annotations) {
            item.data.selected = false;
        }
    } else {
        auto it = engine->id_map.find(annotation_id);
        if (it != engine->id_map.end()) {
            it->second->data.selected = selected;
        }
    }
    
    return 0;
}

int ar_engine_move(AROverlayEngine* engine, uint32_t annotation_id, float dx, float dy) {
    if (!engine) return -1;
    
    auto it = engine->id_map.find(annotation_id);
    if (it == engine->id_map.end()) return -1;
    
    it->second->data.x += dx;
    it->second->data.y += dy;
    
    return 0;
}

int ar_engine_scale(AROverlayEngine* engine, uint32_t annotation_id,
                    float scale, float center_x, float center_y) {
    if (!engine) return -1;
    
    auto it = engine->id_map.find(annotation_id);
    if (it == engine->id_map.end()) return -1;
    
    AnnotationData& ann = it->second->data;
    
    // 相对于中心点缩放
    float old_cx = ann.x + ann.width / 2;
    float old_cy = ann.y + ann.height / 2;
    
    float new_cx = center_x + (old_cx - center_x) * scale;
    float new_cy = center_y + (old_cy - center_y) * scale;
    
    ann.width *= scale;
    ann.height *= scale;
    ann.x = new_cx - ann.width / 2;
    ann.y = new_cy - ann.height / 2;
    
    return 0;
}

// ============================================================================
// 预设样式
// ============================================================================

void ar_get_anatomy_style(AnnotationStyle* style) {
    if (!style) return;
    memset(style, 0, sizeof(AnnotationStyle));
    style->color_r = 0;
    style->color_g = 128;
    style->color_b = 255;
    style->color_a = 255;
    style->line_width = 2.0f;
    style->font_size = 14.0f;
    style->dashed = true;
}

void ar_get_surgical_style(AnnotationStyle* style) {
    if (!style) return;
    memset(style, 0, sizeof(AnnotationStyle));
    style->color_r = 255;
    style->color_g = 0;
    style->color_b = 0;
    style->color_a = 255;
    style->line_width = 3.0f;
    style->font_size = 16.0f;
}

void ar_get_measurement_style(AnnotationStyle* style) {
    if (!style) return;
    memset(style, 0, sizeof(AnnotationStyle));
    style->color_r = 255;
    style->color_g = 255;
    style->color_b = 0;
    style->color_a = 255;
    style->line_width = 1.5f;
    style->font_size = 12.0f;
}

void ar_get_warning_style(AnnotationStyle* style) {
    if (!style) return;
    memset(style, 0, sizeof(AnnotationStyle));
    style->color_r = 255;
    style->color_g = 165;
    style->color_b = 0;
    style->color_a = 255;
    style->line_width = 4.0f;
    style->font_size = 18.0f;
}

} // extern "C"
