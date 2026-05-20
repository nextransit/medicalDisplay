cmake_minimum_required(VERSION 3.20)

if(APPLE AND CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    if(EXISTS "/opt/homebrew/opt/gcc/bin/gcc-15" AND EXISTS "/opt/homebrew/opt/gcc/bin/g++-15")
        set(CMAKE_C_COMPILER "/opt/homebrew/opt/gcc/bin/gcc-15" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/gcc/bin/g++-15" CACHE FILEPATH "CXX compiler" FORCE)
    elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang" AND EXISTS "/opt/homebrew/opt/llvm/bin/clang++")
        set(CMAKE_C_COMPILER "/opt/homebrew/opt/llvm/bin/clang" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm/bin/clang++" CACHE FILEPATH "CXX compiler" FORCE)
    endif()
endif()

if(APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MEDICALDISPLAY_MACOS_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(MEDICALDISPLAY_MACOS_SDK)
        set(CMAKE_OSX_SYSROOT "${MEDICALDISPLAY_MACOS_SDK}" CACHE PATH "macOS SDK" FORCE)
    endif()
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(MedicalDisplayTests LANGUAGES C CXX)
    enable_testing()
endif()

get_filename_component(MEDICALDISPLAY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_library(medicaldisplay_test_support STATIC
    support/display_linux_stubs.cpp
    support/drm_mock.cpp
    ${MEDICALDISPLAY_ROOT}/sdk/ai_engine/src/ai_engine_impl.cpp
    ${MEDICALDISPLAY_ROOT}/sdk/ai_engine/src/onnx_backend.cpp
    ${MEDICALDISPLAY_ROOT}/sdk/display_engine/src/display_engine_impl.cpp
    ${MEDICALDISPLAY_ROOT}/sdk/dicom/src/dicom_reader.cpp
    ${MEDICALDISPLAY_ROOT}/sdk/dicom/src/dicom_gsdf.c
    ${MEDICALDISPLAY_ROOT}/sdk/surgical_video/src/surgical_video.cpp
    ${MEDICALDISPLAY_ROOT}/sdk/platform/linux/src/drm_display.cpp
)

target_include_directories(medicaldisplay_test_support
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/support
        ${CMAKE_CURRENT_SOURCE_DIR}/support/drm
        ${MEDICALDISPLAY_ROOT}/sdk/ai_engine/include
        ${MEDICALDISPLAY_ROOT}/sdk/common/include
        ${MEDICALDISPLAY_ROOT}/sdk/display_engine/include
        ${MEDICALDISPLAY_ROOT}/sdk/dicom/include
        ${MEDICALDISPLAY_ROOT}/sdk/surgical_video/include
        ${MEDICALDISPLAY_ROOT}/sdk/platform/linux/include
        ${MEDICALDISPLAY_ROOT}/sdk/cloud/include
        ${MEDICALDISPLAY_ROOT}/sdk/predictive_maintenance/include
        ${MEDICALDISPLAY_ROOT}/sdk/federated/include
        ${MEDICALDISPLAY_ROOT}/sdk/multimodal/include
        ${MEDICALDISPLAY_ROOT}/sdk/multiscreen/include
        ${MEDICALDISPLAY_ROOT}/sdk/ar_overlay/include
        ${MEDICALDISPLAY_ROOT}/sdk/digital_twin/include
)

target_compile_features(medicaldisplay_test_support PUBLIC cxx_std_17)
target_compile_definitions(medicaldisplay_test_support PUBLIC MEDICALDISPLAY_FORCE_LINUX_DISPLAY)

target_link_libraries(medicaldisplay_test_support
    PRIVATE
        Threads::Threads
        ZLIB::ZLIB
        medicaldisplay_simd
        medicaldisplay_predictive
        medicaldisplay_federated
        medicaldisplay_multimodal
        medicaldisplay_multiscreen
        medicaldisplay_ar_overlay
        medicaldisplay_digital_twin
)

if(APPLE)
    target_link_libraries(medicaldisplay_test_support PUBLIC c++)
endif()

function(add_medicaldisplay_gtest target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name}
        PRIVATE
            medicaldisplay_test_support
            GTest::gtest_main
    )
    target_include_directories(${target_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/support
            ${CMAKE_CURRENT_SOURCE_DIR}/support/drm
            ${MEDICALDISPLAY_ROOT}/sdk/ai_engine/include
            ${MEDICALDISPLAY_ROOT}/sdk/common/include
            ${MEDICALDISPLAY_ROOT}/sdk/display_engine/include
            ${MEDICALDISPLAY_ROOT}/sdk/dicom/include
            ${MEDICALDISPLAY_ROOT}/sdk/surgical_video/include
            ${MEDICALDISPLAY_ROOT}/sdk/platform/linux/include
            ${MEDICALDISPLAY_ROOT}/sdk/cloud/include
            ${MEDICALDISPLAY_ROOT}/sdk/performance/include
            ${MEDICALDISPLAY_ROOT}/sdk/predictive_maintenance/include
            ${MEDICALDISPLAY_ROOT}/sdk/federated/include
            ${MEDICALDISPLAY_ROOT}/sdk/multimodal/include
            ${MEDICALDISPLAY_ROOT}/sdk/multiscreen/include
            ${MEDICALDISPLAY_ROOT}/sdk/ar_overlay/include
            ${MEDICALDISPLAY_ROOT}/sdk/digital_twin/include
    )
    if(APPLE)
        target_link_libraries(${target_name} PRIVATE c++)
    endif()
    target_compile_features(${target_name} PRIVATE cxx_std_17)
    gtest_discover_tests(${target_name})
endfunction()

include(GoogleTest)

add_medicaldisplay_gtest(test_suite
    test_suite.cpp
)

add_medicaldisplay_gtest(test_display_pipeline
    test_display_pipeline.cpp
)

add_medicaldisplay_gtest(test_drm
    test_drm.cpp
)

add_medicaldisplay_gtest(test_gsdf
    test_gsdf.cpp
    test_gsdf_e2e.cpp
)

add_medicaldisplay_gtest(test_cloud_security
    test_cloud_security.cpp
)

add_medicaldisplay_gtest(test_dicom_security
    test_dicom_security.cpp
)

add_medicaldisplay_gtest(test_dicom_fuzz
    test_dicom_fuzz.cpp
)

# ========================================================================
# v2.0 新增模块单元测试
# ========================================================================

add_medicaldisplay_gtest(test_predictive_maintenance
    test_predictive_maintenance.cpp
)

add_medicaldisplay_gtest(test_federated
    test_federated.cpp
)

add_medicaldisplay_gtest(test_multimodal
    test_multimodal.cpp
)

add_medicaldisplay_gtest(test_multiscreen
    test_multiscreen.cpp
)

add_medicaldisplay_gtest(test_ar_overlay
    test_ar_overlay.cpp
)

add_medicaldisplay_gtest(test_digital_twin
    test_digital_twin.cpp
)

add_medicaldisplay_gtest(test_performance
    test_performance.cpp
)
target_link_libraries(test_performance PRIVATE medicaldisplay_performance)


# ========================================================================
# 全链路端到端集成测试
# ========================================================================

add_medicaldisplay_gtest(test_e2e_pipeline
    test_e2e_pipeline.cpp
)


# SIMD性能基准测试
add_executable(test_simd_benchmark test_simd_benchmark.cpp)
target_link_libraries(test_simd_benchmark PRIVATE
    medicaldisplay_test_support
    medicaldisplay_performance
    GTest::gtest_main
)
target_include_directories(test_simd_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/support/drm
    ${MEDICALDISPLAY_ROOT}/sdk/ai_engine/include
    ${MEDICALDISPLAY_ROOT}/sdk/common/include
    ${MEDICALDISPLAY_ROOT}/sdk/display_engine/include
    ${MEDICALDISPLAY_ROOT}/sdk/dicom/include
    ${MEDICALDISPLAY_ROOT}/sdk/performance/include
    ${MEDICALDISPLAY_ROOT}/sdk/surgical_video/include
    ${MEDICALDISPLAY_ROOT}/sdk/multimodal/include
)
if(APPLE)
    target_link_libraries(test_simd_benchmark PRIVATE c++)
endif()
target_compile_features(test_simd_benchmark PRIVATE cxx_std_17)
gtest_discover_tests(test_simd_benchmark)
