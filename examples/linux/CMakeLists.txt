cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MedicalDisplayExamples LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(APPLE AND (NOT CMAKE_OSX_SYSROOT OR CMAKE_OSX_SYSROOT STREQUAL ""))
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE MEDICALDISPLAY_MACOS_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(MEDICALDISPLAY_MACOS_SDK)
        set(CMAKE_OSX_SYSROOT "${MEDICALDISPLAY_MACOS_SDK}" CACHE PATH "macOS SDK path" FORCE)
    endif()
endif()

if(APPLE AND CMAKE_OSX_SYSROOT AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(MEDICALDISPLAY_MACOS_LIBCXX_DIR "${CMAKE_OSX_SYSROOT}/usr/include/c++/v1")
    if(EXISTS "${MEDICALDISPLAY_MACOS_LIBCXX_DIR}/cstddef")
        add_compile_options(-isystem "${MEDICALDISPLAY_MACOS_LIBCXX_DIR}")
    endif()
endif()

if(NOT TARGET Threads::Threads)
    find_package(Threads REQUIRED)
endif()

set(MEDICALDISPLAY_EXAMPLES_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(MEDICALDISPLAY_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk)

add_library(medical_demo_support STATIC
    demo_support.cpp
    ${MEDICALDISPLAY_SDK_ROOT}/dicom/src/dicom_gsdf.c
)

target_include_directories(medical_demo_support PUBLIC
    ${MEDICALDISPLAY_EXAMPLES_ROOT}
    ${MEDICALDISPLAY_SDK_ROOT}/dicom/include
)

target_link_libraries(medical_demo_support PUBLIC Threads::Threads)

if(UNIX AND NOT APPLE)
    target_link_libraries(medical_demo_support PUBLIC m)
endif()

# Basic demos
foreach(target_name medical_display_demo dicom_receiver multi_display_demo gsdf_calibration_demo cloud_demo)
    add_executable(${target_name} ${target_name}.cpp)
    target_link_libraries(${target_name} PRIVATE medical_demo_support Threads::Threads)
endforeach()

# Predictive maintenance demo
add_executable(predictive_maintenance_demo predictive_maintenance_demo.cpp)
target_link_libraries(predictive_maintenance_demo PRIVATE
    Threads::Threads
    medicaldisplay_predictive
    medicaldisplay_common
    medicaldisplay_simd
)
target_include_directories(predictive_maintenance_demo PRIVATE
    ${MEDICALDISPLAY_SDK_ROOT}/predictive_maintenance/include
    ${MEDICALDISPLAY_SDK_ROOT}/common/include
)

# Surgical video demo
add_executable(surgical_video_demo surgical_video_demo.cpp)
target_link_libraries(surgical_video_demo PRIVATE
    Threads::Threads
    medicaldisplay_surgical
)
target_include_directories(surgical_video_demo PRIVATE
    ${MEDICALDISPLAY_SDK_ROOT}/surgical_video/include
)

# V4L2采集Demo (Linux only)
if(BUILD_PLATFORM_LINUX)
    add_executable(v4l2_capture_demo v4l2_capture_demo.cpp)
    target_include_directories(v4l2_capture_demo PRIVATE
        ${MEDICALDISPLAY_SDK_ROOT}/platform/linux/include
        ${MEDICALDISPLAY_SDK_ROOT}/performance/include
        ${MEDICALDISPLAY_SDK_ROOT}/surgical_video/include
    )
    target_link_libraries(v4l2_capture_demo PRIVATE
        medicaldisplay_surgical
        medicaldisplay_simd
        Threads::Threads
    )
    add_test(NAME Smoke_v4l2_capture_demo COMMAND v4l2_capture_demo --help)
endif()
