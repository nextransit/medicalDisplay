cmake_minimum_required(VERSION 3.20)
project(MedicalDisplaySDK VERSION 1.0.0 LANGUAGES C CXX)

# ============================================================================
# Project Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译器设置 (macOS优先使用g++-15，用户已指定的编译器优先)
if(APPLE)
    find_program(CXX_COMPILER_NAMES g++-15 g++-14 g++-13)
    if(CXX_COMPILER_NAMES)
        message(STATUS "Found C++ compiler: ${CXX_COMPILER_NAMES}")
        if(NOT CMAKE_C_COMPILER)
            set(CMAKE_C_COMPILER "/usr/bin/cc")
        endif()
        if(NOT CMAKE_CXX_COMPILER)
            set(CMAKE_CXX_COMPILER "${CXX_COMPILER_NAMES}")
        endif()
    endif()
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

# ============================================================================
# SDK Components
# ============================================================================

add_subdirectory(sdk/common)
add_subdirectory(sdk/ai_engine)
add_subdirectory(sdk/display_engine)
add_subdirectory(sdk/dicom)
add_subdirectory(sdk/cloud)

# Platform-specific
if(LINUX)
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
