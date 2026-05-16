cmake_minimum_required(VERSION 3.20)
project(MedicalDisplaySDK VERSION 1.0.0 LANGUAGES C CXX)

# ============================================================================
# Project Configuration
# ============================================================================

# [P1-FIX] 添加 BUILD_PLATFORM_LINUX 选项
option(BUILD_PLATFORM_LINUX "Build for Linux platform" OFF)
option(ENABLE_VULKAN "Enable experimental Vulkan rendering support" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译器设置 — 使用 Apple Clang（系统默认），自动修复 C++ headers 搜索路径
if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE _xcrun_sdk_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_xcrun_sdk_path AND EXISTS "${_xcrun_sdk_path}/usr/include/c++/v1")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -cxx-isystem ${_xcrun_sdk_path}/usr/include/c++/v1")
    endif()
    unset(_xcrun_sdk_path)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
endif()

# Export compile commands for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Find Dependencies
# ============================================================================

# Threads
find_package(Threads REQUIRED)

# Highway SIMD library (optional - will use scalar fallback if not found)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HWY QUIET libhwy)
endif()
if(NOT HWY_FOUND)
    find_library(HWY_LIBRARY NAMES hwy PATHS /opt/homebrew/lib /usr/local/lib)
endif()
if(HWY_FOUND OR HWY_LIBRARY)
    message(STATUS "Highway SIMD library found")
endif()

# ============================================================================
# SDK Components
# ============================================================================

add_subdirectory(sdk/common)
add_subdirectory(sdk/ai_engine)
add_subdirectory(sdk/display_engine)
add_subdirectory(sdk/dicom)
add_subdirectory(sdk/cloud)
add_subdirectory(sdk/multimodal)
add_subdirectory(sdk/multiscreen)
add_subdirectory(sdk/predictive_maintenance)
add_subdirectory(sdk/federated)
add_subdirectory(sdk/surgical_video)

# Platform-specific
if(BUILD_PLATFORM_LINUX OR (CMAKE_SYSTEM_NAME MATCHES "Linux"))
    add_subdirectory(sdk/platform/linux)
endif()

if(ANDROID)
    add_subdirectory(sdk/platform/android)
endif()

# ============================================================================
# Examples
# ============================================================================

option(BUILD_EXAMPLES "Build example applications" ON)

if(BUILD_EXAMPLES)
    if(LINUX OR APPLE)
        add_subdirectory(examples/linux)
    endif()
    
    if(ANDROID)
        add_subdirectory(examples/android)
    endif()
endif()

# ============================================================================
# Smoke Tests (Example Binaries as Tests)
# ============================================================================

if(BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/linux")
    add_test(NAME Smoke_medical_display_demo COMMAND medical_display_demo --frames 2 --width 64 --height 64)
    add_test(NAME Smoke_gsdf_calibration_demo COMMAND gsdf_calibration_demo)
    add_test(NAME Smoke_cloud_demo COMMAND cloud_demo)
endif()

# ============================================================================
# Tests
# ============================================================================

option(BUILD_TESTS "Build test applications" ON)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Installation
# ============================================================================

include(GNUInstallDirs)

install(DIRECTORY sdk/ 
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay
        FILES_MATCHING PATTERN "*.h")

# ============================================================================
# Package Configuration
# ============================================================================

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/MedicalDisplaySDKConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MedicalDisplaySDKConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MedicalDisplaySDK
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MedicalDisplaySDKVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES 
    ${CMAKE_CURRENT_BINARY_DIR}/MedicalDisplaySDKConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MedicalDisplaySDKVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MedicalDisplaySDK
)
