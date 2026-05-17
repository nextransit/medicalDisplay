/**
 * OpenGL 后端 — 空桩实现
 *
 * 当前项目在 macOS 上使用 Metal 后端。
 * 此文件为非 Apple 平台提供占位实现，
 * 以便 CMakeLists.txt 中的源文件列表完整。
 * 后续可替换为实际的 OpenGL 渲染实现。
 */

// 返回 nullptr 表示 OpenGL 不可用，自动回退到 CPU 渲染路径
extern "C" {

void* opengl_init(int /*width*/, int /*height*/) {
    return nullptr;  // OpenGL 后端尚未实现
}

void opengl_render(void* /*ctx*/, const void* /*params*/, void* /*out_pixels*/) {
    // 空实现
}

void opengl_destroy(void* /*ctx*/) {
    // 空实现
}

}  // extern "C"
