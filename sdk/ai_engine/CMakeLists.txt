# AI Engine CMakeLists.txt
add_library(medicaldisplay_ai STATIC
    src/ai_engine_impl.cpp
    src/onnx_backend.cpp
)
add_library(MedicalDisplay::AI ALIAS medicaldisplay_ai)

target_include_directories(medicaldisplay_ai PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(medicaldisplay_ai PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/sdk/common/include>
)

target_compile_features(medicaldisplay_ai PUBLIC cxx_std_17)

# Dependencies
target_link_libraries(medicaldisplay_ai PUBLIC
    Threads::Threads
)

# ONNX Runtime support (Homebrew on macOS)
if(APPLE)
    execute_process(
        COMMAND brew --prefix onnxruntime
        OUTPUT_VARIABLE ONNXRUNTIME_HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(ONNXRUNTIME_HOMEBREW_PREFIX AND EXISTS "${ONNXRUNTIME_HOMEBREW_PREFIX}/include/onnxruntime/onnxruntime_cxx_api.h")
        target_include_directories(medicaldisplay_ai PUBLIC "${ONNXRUNTIME_HOMEBREW_PREFIX}/include")
        find_library(ONNXRUNTIME_LIB onnxruntime "${ONNXRUNTIME_HOMEBREW_PREFIX}/lib" REQUIRED)
        if(ONNXRUNTIME_LIB)
            target_link_libraries(medicaldisplay_ai PUBLIC "${ONNXRUNTIME_LIB}")
            target_compile_definitions(medicaldisplay_ai PUBLIC AI_BACKEND_ONNXRUNTIME)
            message(STATUS "ONNX Runtime found at ${ONNXRUNTIME_HOMEBREW_PREFIX}")
        endif()
    endif()
elseif(UNIX)
    # Linux: try system ONNX Runtime
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        PATHS /usr/include/onnxruntime /usr/local/include/onnxruntime
    )
    find_library(ONNXRUNTIME_LIB onnxruntime
        PATHS /usr/lib /usr/local/lib
    )
    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
        target_include_directories(medicaldisplay_ai PUBLIC "${ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(medicaldisplay_ai PUBLIC "${ONNXRUNTIME_LIB}")
        target_compile_definitions(medicaldisplay_ai PUBLIC AI_BACKEND_ONNXRUNTIME)
        message(STATUS "ONNX Runtime found")
    endif()
endif()

# NPU support (RKNN)
if(ANDROID)
    find_library(RKNN_LIBRARY rknn_runtime)
    if(RKNN_LIBRARY)
        target_link_libraries(medicaldisplay_ai PUBLIC ${RKNN_LIBRARY})
        target_compile_definitions(medicaldisplay_ai PUBLIC AI_NPU_RKNN)
    endif()
endif()

# OpenVINO support
if(UNIX AND NOT ANDROID AND NOT APPLE)
    find_package(OpenVINO QUIET)
    if(OpenVINO_FOUND)
        target_link_libraries(medicaldisplay_ai PUBLIC openvino::runtime)
        target_compile_definitions(medicaldisplay_ai PUBLIC AI_GPU_INTEL)
    endif()
endif()

# TensorRT support
if(UNIX AND NOT APPLE AND CMAKE_CUDA_COMPILER)
    find_package(TensorRT QUIET)
    if(TensorRT_FOUND)
        target_link_libraries(medicaldisplay_ai PUBLIC ${TENSORRT_LIBRARIES})
        target_compile_definitions(medicaldisplay_ai PUBLIC AI_GPU_TENSORRT)
    endif()
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(medicaldisplay_ai PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_ai PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_ai
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/ai
        FILES_MATCHING PATTERN "*.h")
