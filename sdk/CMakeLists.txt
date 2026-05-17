cmake_minimum_required(VERSION 3.20)
project(MedicalDisplaySDK VERSION 1.0.0 LANGUAGES C CXX)

# ============================================================================
# 配置选项
# ============================================================================
option(BUILD_AI_ENGINE "Build AI inference engine" ON)
option(BUILD_DISPLAY_ENGINE "Build display rendering engine" ON)
option(BUILD_DICOM "Build DICOM processing library" ON)
option(BUILD_PLATFORM_LINUX "Build Linux platform support" ON)
option(BUILD_PLATFORM_ANDROID "Build Android platform support" OFF)
option(BUILD_CLOUD "Build cloud sync agent" ON)
option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_VULKAN "Enable Vulkan rendering" ON)
option(ENABLE_OPENGL "Enable OpenGL rendering" ON)
option(ENABLE_NPU "Enable NPU acceleration" ON)
option(ENABLE_GPU "Enable GPU acceleration" ON)

# ============================================================================
# 编译器设置
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 警告
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-fPIC)
endif()

# 调试/发布
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -DDEBUG)
else()
    add_compile_options(-O3 -DNDEBUG)
endif()

# ============================================================================
# 依赖
# ============================================================================
find_package(Threads REQUIRED)
find_package(PkgConfig)

# Vulkan
if(ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

# OpenGL
if(ENABLE_OPENGL)
    pkg_check_modules(GL REQUIRED gl)
endif()

# JSON处理
find_path(JSON_INCLUDE_DIR jsoncpp/json.h)
find_library(JSON_LIBRARY NAMES jsoncpp libjsoncpp)

# ============================================================================
# 目录结构
# ============================================================================
set(SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(INCLUDE_DIR ${SDK_ROOT}/common/include)
set(SOURCE_DIR ${SDK_ROOT}/common/src)

# AI Engine
if(BUILD_AI_ENGINE)
    set(AI_INCLUDE_DIR ${SDK_ROOT}/ai_engine/include)
    set(AI_SOURCE_DIR ${SDK_ROOT}/ai_engine/src)
endif()

# Display Engine
if(BUILD_DISPLAY_ENGINE)
    set(DISPLAY_INCLUDE_DIR ${SDK_ROOT}/display_engine/include)
    set(DISPLAY_SHADER_DIR ${SDK_ROOT}/display_engine/shaders)
endif()

# DICOM
if(BUILD_DICOM)
    set(DICOM_INCLUDE_DIR ${SDK_ROOT}/dicom/include)
    set(DICOM_SOURCE_DIR ${SDK_ROOT}/dicom/src)
endif()

# Linux Platform
if(BUILD_PLATFORM_LINUX)
    set(LINUX_INCLUDE_DIR ${SDK_ROOT}/platform/linux/include)
    set(LINUX_SOURCE_DIR ${SDK_ROOT}/platform/linux/src)
endif()

# Cloud
if(BUILD_CLOUD)
    set(CLOUD_INCLUDE_DIR ${SDK_ROOT}/cloud/include)
    set(CLOUD_SOURCE_DIR ${SDK_ROOT}/cloud/src)
endif()

# ============================================================================
# 公共包含目录
# ============================================================================
include_directories(
    ${INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# ============================================================================
# 静态库: medical_display_common
# ============================================================================
set(COMMON_SOURCES
    ${SOURCE_DIR}/logging.c
    ${SOURCE_DIR}/memory_pool.c
    ${SOURCE_DIR}/ring_buffer.c
)

add_library(medical_display_common STATIC ${COMMON_SOURCES})
target_include_directories(medical_display_common PUBLIC ${INCLUDE_DIR})
target_link_libraries(medical_display_common PUBLIC Threads::Threads)

# ============================================================================
# AI Engine
# ============================================================================
if(BUILD_AI_ENGINE)
    set(AI_SOURCES
        ${AI_SOURCE_DIR}/ai_engine_impl.cpp
        ${AI_SOURCE_DIR}/onnx_backend.cpp
    )

    add_library(medical_display_ai STATIC ${AI_SOURCES})
    target_include_directories(medical_display_ai PUBLIC ${AI_INCLUDE_DIR})
    target_link_libraries(medical_display_ai PUBLIC medical_display_common)
    
    if(ENABLE_NPU)
        target_compile_definitions(medical_display_ai PUBLIC WITH_NPU=1)
        target_link_libraries(medical_display_ai PUBLIC rt)
    endif()
    
    if(ENABLE_GPU)
        target_compile_definitions(medical_display_ai PUBLIC WITH_GPU=1)
        if(Vulkan_FOUND)
            target_link_libraries(medical_display_ai PUBLIC Vulkan::vulkan)
        endif()
    endif()
endif()

# ============================================================================
# DICOM Library
# ============================================================================
if(BUILD_DICOM)
    set(DICOM_SOURCES
        ${DICOM_SOURCE_DIR}/dicom_gsdf.c
        ${DICOM_SOURCE_DIR}/dicom_parser.c
        ${DICOM_SOURCE_DIR}/dicom_color.c
    )
    
    add_library(medical_display_dicom STATIC ${DICOM_SOURCES})
    target_include_directories(medical_display_dicom PUBLIC ${DICOM_INCLUDE_DIR})
    target_link_libraries(medical_display_dicom PUBLIC medical_display_common m)
endif()

# ============================================================================
# Display Engine
# ============================================================================
if(BUILD_DISPLAY_ENGINE)
    set(DISPLAY_SOURCES
        ${SDK_ROOT}/display_engine/src/display_engine_impl.cpp
    )
    
    add_library(medical_display_display STATIC ${DISPLAY_SOURCES})
    target_include_directories(medical_display_display PUBLIC ${DISPLAY_INCLUDE_DIR})
    target_link_libraries(medical_display_display PUBLIC medical_display_common)
    
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        target_compile_definitions(medical_display_display PUBLIC WITH_VULKAN=1)
        target_link_libraries(medical_display_display PUBLIC Vulkan::vulkan)
        
        # 复制Shader到构建目录
        add_custom_command(TARGET medical_display_display POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${DISPLAY_SHADER_DIR}
            $<TARGET_FILE_DIR:medical_display_display>/shaders
        )
    endif()
    
    if(ENABLE_OPENGL AND GL_FOUND)
        target_compile_definitions(medical_display_display PUBLIC WITH_OPENGL=1)
        target_link_libraries(medical_display_display PUBLIC ${GL_LIBRARIES})
    endif()
endif()

# ============================================================================
# Linux Platform
# ============================================================================
if(BUILD_PLATFORM_LINUX)
    set(LINUX_SOURCES
        ${LINUX_SOURCE_DIR}/linux_display_impl.c
    )
    
    add_library(medical_display_linux STATIC ${LINUX_SOURCES})
    target_include_directories(medical_display_linux PUBLIC ${LINUX_INCLUDE_DIR})
    target_link_libraries(medical_display_linux PUBLIC 
        medical_display_common
        medical_display_display
        dl
    )
    
    # DRM/Libdrm
    pkg_check_modules(LIBDRM REQUIRED libdrm)
    target_include_directories(medical_display_linux PUBLIC ${LIBDRM_INCLUDE_DIRS})
    target_link_libraries(medical_display_linux PUBLIC ${LIBDRM_LIBRARIES})
endif()

# ============================================================================
# Cloud Agent
# ============================================================================
if(BUILD_CLOUD)
    set(CLOUD_SOURCES
        ${CLOUD_SOURCE_DIR}/cloud_agent_impl.c
        ${CLOUD_SOURCE_DIR}/mqtt_client.c
        ${CLOUD_SOURCE_DIR}/ota_downloader.c
    )
    
    add_library(medical_display_cloud STATIC ${CLOUD_SOURCES})
    target_include_directories(medical_display_cloud PUBLIC ${CLOUD_INCLUDE_DIR})
    target_link_libraries(medical_display_cloud PUBLIC medical_display_common)
    
    # CURL
    find_package(CURL REQUIRED)
    target_link_libraries(medical_display_cloud PUBLIC CURL::libcurl)
    
    # OpenSSL
    find_package(OpenSSL REQUIRED)
    target_link_libraries(medical_display_cloud PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    
    # JsonCpp
    if(JSON_LIBRARY)
        target_link_libraries(medical_display_cloud PUBLIC ${JSON_LIBRARY})
    endif()
endif()

# ============================================================================
# 完整SDK库
# ============================================================================
set(SDK_LIBS medical_display_common)

if(BUILD_AI_ENGINE)
    list(APPEND SDK_LIBS medical_display_ai)
endif()

if(BUILD_DICOM)
    list(APPEND SDK_LIBS medical_display_dicom)
endif()

if(BUILD_DISPLAY_ENGINE)
    list(APPEND SDK_LIBS medical_display_display)
endif()

if(BUILD_PLATFORM_LINUX)
    list(APPEND SDK_LIBS medical_display_linux)
endif()

if(BUILD_CLOUD)
    list(APPEND SDK_LIBS medical_display_cloud)
endif()

add_library(MedicalDisplaySDK STATIC IMPORTED)
set_target_properties(MedicalDisplaySDK PROPERTIES IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/libMedicalDisplaySDK.a)

# ============================================================================
# Examples
# ============================================================================
if(BUILD_EXAMPLES)
    add_subdirectory(${SDK_ROOT}/examples/linux)
endif()

# ============================================================================
# Tests
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(${SDK_ROOT}/tests)
endif()

# ============================================================================
# 导出
# ============================================================================
install(DIRECTORY ${SDK_ROOT}/common/include/
        DESTINATION include/medical_display)

install(DIRECTORY ${SDK_ROOT}/ai_engine/include/
        DESTINATION include/medical_display/ai
        OPTIONAL)

install(DIRECTORY ${SDK_ROOT}/display_engine/include/
        DESTINATION include/medical_display/display
        OPTIONAL)

install(DIRECTORY ${SDK_ROOT}/dicom/include/
        DESTINATION include/medical_display/dicom
        OPTIONAL)

install(DIRECTORY ${SDK_ROOT}/platform/linux/include/
        DESTINATION include/medical_display/platform
        OPTIONAL)

install(DIRECTORY ${SDK_ROOT}/cloud/include/
        DESTINATION include/medical_display/cloud
        OPTIONAL)

# ============================================================================
# 配置信息
# ============================================================================
message(STATUS "========================================")
message(STATUS "Medical Display SDK Configuration")
message(STATUS "========================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Options:")
message(STATUS "  BUILD_AI_ENGINE: ${BUILD_AI_ENGINE}")
message(STATUS "  BUILD_DISPLAY_ENGINE: ${BUILD_DISPLAY_ENGINE}")
message(STATUS "  BUILD_DICOM: ${BUILD_DICOM}")
message(STATUS "  BUILD_PLATFORM_LINUX: ${BUILD_PLATFORM_LINUX}")
message(STATUS "  BUILD_CLOUD: ${BUILD_CLOUD}")
message(STATUS "  ENABLE_VULKAN: ${ENABLE_VULKAN}")
message(STATUS "  ENABLE_OPENGL: ${ENABLE_OPENGL}")
message(STATUS "  ENABLE_NPU: ${ENABLE_NPU}")
message(STATUS "  ENABLE_GPU: ${ENABLE_GPU}")
message(STATUS "========================================")

# ============================================================================
# Performance模块 (SIMD加速)
# ============================================================================
set(PERFORMANCE_INCLUDE_DIR ${SDK_ROOT}/performance/include)
set(PERFORMANCE_SOURCE_DIR ${SDK_ROOT}/performance/src)

set(SIMD_SOURCES
    ${PERFORMANCE_SOURCE_DIR}/simd_neon.cpp
    ${PERFORMANCE_SOURCE_DIR}/simd_processing.cpp
    ${PERFORMANCE_SOURCE_DIR}/simd_processing_extended.cpp
    ${PERFORMANCE_SOURCE_DIR}/memory_pool.cpp
)

add_library(medical_display_simd STATIC ${SIMD_SOURCES})
target_include_directories(medical_display_simd PUBLIC ${PERFORMANCE_INCLUDE_DIR})
target_link_libraries(medical_display_simd PUBLIC medical_display_common)

# ============================================================================
# Surgical Video模块 (GPU加速)
# ============================================================================
set(SURGICAL_INCLUDE_DIR ${SDK_ROOT}/surgical_video/include)
set(SURGICAL_SOURCE_DIR ${SDK_ROOT}/surgical_video/src)

set(SURGICAL_SOURCES
    ${SURGICAL_SOURCE_DIR}/surgical_video.cpp
    ${SURGICAL_SOURCE_DIR}/surgical_video_optimized.cpp
    ${SURGICAL_SOURCE_DIR}/gpu_pipeline_cpu_fallback.cpp
)

if(ENABLE_VULKAN)
    list(APPEND SURGICAL_SOURCES ${SURGICAL_SOURCE_DIR}/vulkan_compute.cpp)
endif()

add_library(medical_display_surgical STATIC ${SURGICAL_SOURCES})
target_include_directories(medical_display_surgical PUBLIC ${SURGICAL_INCLUDE_DIR} ${INCLUDE_DIR} ${PERFORMANCE_INCLUDE_DIR} ${SDK_ROOT}/common/include)
target_link_libraries(medical_display_surgical PUBLIC medical_display_simd)

if(ENABLE_VULKAN AND Vulkan_FOUND)
    target_link_libraries(medical_display_surgical PUBLIC Vulkan::vulkan)
endif()

# ============================================================================
# Multimodal模块 (3D体绘制)
# ============================================================================
set(MULTIMODAL_INCLUDE_DIR ${SDK_ROOT}/multimodal/include)
set(MULTIMODAL_SOURCE_DIR ${SDK_ROOT}/multimodal/src)

set(MULTIMODAL_SOURCES
    ${MULTIMODAL_SOURCE_DIR}/multimodal_fusion.cpp
    ${MULTIMODAL_SOURCE_DIR}/volume_renderer.cpp
)

add_library(medical_display_multimodal STATIC ${MULTIMODAL_SOURCES})
target_include_directories(medical_display_multimodal PUBLIC ${MULTIMODAL_INCLUDE_DIR})
target_link_libraries(medical_display_multimodal PUBLIC medical_display_simd)

