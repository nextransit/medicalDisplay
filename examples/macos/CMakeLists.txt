# AI 自适应医疗显示系统 - macOS 构建配置
# 
# 支持 macOS Metal GPU 加速演示

cmake_minimum_required(VERSION 3.20)
project(MedicalDisplayMacOS VERSION 1.0.0 LANGUAGES CXX Swift)

# -----------------------------------------------------------------------------
# 平台检测
# -----------------------------------------------------------------------------
if(NOT APPLE)
    message(FATAL_ERROR "此配置仅支持 macOS")
endif()

# -----------------------------------------------------------------------------
# Metal / Swift 支持
# -----------------------------------------------------------------------------
find_package(Metal REQUIRED)
find_package(Foundation REQUIRED)

set(SWIFT_FLAGS "-Xfrontend" "-warn-long-function-bodies=100")

# -----------------------------------------------------------------------------
# 可执行目标: MetalMedicalDemo
# -----------------------------------------------------------------------------
add_executable(MetalMedicalDemo
    MetalMedicalDemo.swift
)

target_compile_options(MetalMedicalDemo PRIVATE
    ${SWIFT_FLAGS}
)

target_link_libraries(MetalMedicalDemo PRIVATE
    -framework Metal
    -framework MetalKit
    -framework Foundation
    -framework simd
)

# 设置输出目录
set_target_properties(MetalMedicalDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# -----------------------------------------------------------------------------
# 可选: Objective-C++ 包装层 (用于 C SDK 集成)
# -----------------------------------------------------------------------------
add_library(MedicalDisplayMacOS SHARED
    MedicalDisplayBridge.mm
)

target_sources(MedicalDisplayMacOS PRIVATE
    ${CMAKE_SOURCE_DIR}/sdk/surgical_video/src/metal_compute.mm
    ${CMAKE_SOURCE_DIR}/sdk/display_engine/src/display_engine.cpp
    ${CMAKE_SOURCE_DIR}/sdk/ai_engine/src/ai_engine_impl.cpp
)

target_compile_options(MedicalDisplayMacOS PRIVATE
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)

target_link_libraries(MedicalDisplayMacOS PRIVATE
    -framework Metal
    -framework Foundation
    -lc++
)

# -----------------------------------------------------------------------------
# 信息输出
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  AI Medical Display - macOS Build")
message(STATUS "========================================")
message(STATUS "  Metal SDK: ${Metal_VERSION}")
message(STATUS "  Swift: ${SWIFT_VERSION}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")
message(STATUS "")
