# AI Engine CMakeLists.txt
add_library(medicaldisplay_ai INTERFACE)
add_library(MedicalDisplay::AI ALIAS medicaldisplay_ai)

target_include_directories(medicaldisplay_ai INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Platform-specific sources
if(UNIX AND NOT ANDROID)
    target_sources(medicaldisplay_ai INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ai_engine_impl.cpp
    )
endif()

if(ANDROID)
    target_sources(medicaldisplay_ai INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ai_engine_android.cpp
    )
endif()

# Dependencies
target_link_libraries(medicaldisplay_ai INTERFACE
    Threads::Threads
)

# NPU support (RKNN)
if(ANDROID)
    find_library(RKNN_LIBRARY rknn_runtime)
    if(RKNN_LIBRARY)
        target_link_libraries(medicaldisplay_ai INTERFACE ${RKNN_LIBRARY})
        target_compile_definitions(medicaldisplay_ai INTERFACE AI_NPU_RKNN)
    endif()
endif()

# OpenVINO support
if(UNIX AND NOT ANDROID)
    find_package(OpenVINO QUIET)
    if(OpenVINO_FOUND)
        target_link_libraries(medicaldisplay_ai INTERFACE openvino::runtime)
        target_compile_definitions(medicaldisplay_ai INTERFACE AI_GPU_INTEL)
    endif()
endif()

# TensorRT support
if(LINUX AND CMAKE_CUDA_COMPILER)
    find_package(TensorRT QUIET)
    if(TensorRT_FOUND)
        target_link_libraries(medicaldisplay_ai INTERFACE ${TENSORRT_LIBRARIES})
        target_compile_definitions(medicaldisplay_ai INTERFACE AI_GPU_TENSORRT)
    endif()
endif()
