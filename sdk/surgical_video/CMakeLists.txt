# Surgical Video Enhancement Module
add_library(medicaldisplay_surgical STATIC
    src/surgical_video.cpp
    src/gpu_pipeline_cpu_fallback.cpp
    src/surgical_video_optimized.cpp
)

target_include_directories(medicaldisplay_surgical PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/sdk/common/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/sdk/performance/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_surgical PUBLIC cxx_std_17)

# Link with performance module for SIMD and memory pool
if(TARGET medicaldisplay_performance)
    target_link_libraries(medicaldisplay_surgical PUBLIC medicaldisplay_performance)
endif()

target_link_libraries(medicaldisplay_surgical PUBLIC Threads::Threads)

# GPU Pipeline 后端选择
if(ENABLE_VULKAN AND Vulkan_FOUND)
    target_sources(medicaldisplay_surgical PRIVATE src/gpu_pipeline_vulkan.cpp)
    target_compile_definitions(medicaldisplay_surgical PUBLIC SURGICAL_GPU_VULKAN)
    target_link_libraries(medicaldisplay_surgical PRIVATE Vulkan::Vulkan)
    message(STATUS "Surgical Video: GPU=Vulkan")
elseif(ENABLE_OPENGL AND OpenGL_FOUND)
    target_sources(medicaldisplay_surgical PRIVATE src/gpu_pipeline_opengl.cpp)
    target_compile_definitions(medicaldisplay_surgical PUBLIC SURGICAL_GPU_OPENGL)
    target_link_libraries(medicaldisplay_surgical PRIVATE OpenGL::GL)
    message(STATUS "Surgical Video: GPU=OpenGL")
else()
    target_compile_definitions(medicaldisplay_surgical PUBLIC SURGICAL_CPU_ONLY)
    message(STATUS "Surgical Video: GPU=CPU Fallback (SIMD enabled)")
endif()

if(MSVC)
    target_compile_options(medicaldisplay_surgical PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_surgical PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_surgical
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/surgical
        FILES_MATCHING PATTERN "*.h")

install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/medicaldisplay/shaders/surgical
        FILES_MATCHING PATTERN "*.glsl" PATTERN "*.comp" PATTERN "*.spv")
