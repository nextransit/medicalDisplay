# Linux Platform CMakeLists.txt
set(MEDICALDISPLAY_PLATFORM_LINUX_SOURCES)

find_package(Vulkan QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(DRM QUIET libdrm)
endif()

if(Vulkan_FOUND AND DRM_FOUND)
    list(APPEND MEDICALDISPLAY_PLATFORM_LINUX_SOURCES
        src/drm_display.cpp
        src/vulkan_renderer.cpp
    )
else()
    list(APPEND MEDICALDISPLAY_PLATFORM_LINUX_SOURCES
        ${CMAKE_SOURCE_DIR}/tests/support/display_linux_stubs.cpp
    )
endif()

add_library(medicaldisplay_platform_linux STATIC ${MEDICALDISPLAY_PLATFORM_LINUX_SOURCES})

add_library(MedicalDisplay::PlatformLinux ALIAS medicaldisplay_platform_linux)

target_include_directories(medicaldisplay_platform_linux PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(medicaldisplay_platform_linux PUBLIC
    Threads::Threads
)

if(Vulkan_FOUND)
    target_link_libraries(medicaldisplay_platform_linux PUBLIC Vulkan::Vulkan)
endif()

if(DRM_FOUND)
    target_include_directories(medicaldisplay_platform_linux PUBLIC ${DRM_INCLUDE_DIRS})
    target_link_libraries(medicaldisplay_platform_linux PUBLIC ${DRM_LIBRARIES})
    target_compile_definitions(medicaldisplay_platform_linux PUBLIC HAS_LIBDRM)
else()
    target_include_directories(medicaldisplay_platform_linux PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/support
    )
    target_compile_definitions(medicaldisplay_platform_linux PUBLIC MEDICALDISPLAY_FORCE_LINUX_DISPLAY)
endif()

install(TARGETS medicaldisplay_platform_linux
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/platform/linux
)

# ============================================================================
# V4L2视频采集模块 (Linux only - 需要 linux/videodev2.h)
# ============================================================================
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
add_library(medicaldisplay_v4l2 STATIC
    src/v4l2_capture.cpp
)

target_include_directories(medicaldisplay_v4l2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_v4l2 PUBLIC cxx_std_17)

target_include_directories(medicaldisplay_v4l2 PRIVATE
    ${CMAKE_SOURCE_DIR}/sdk/performance/include
)

target_link_libraries(medicaldisplay_v4l2 PUBLIC
    Threads::Threads
    medicaldisplay_platform_linux
    medicaldisplay_performance
)

if(ENABLE_VULKAN AND Vulkan_FOUND)
    target_link_libraries(medicaldisplay_v4l2 PRIVATE Vulkan::Vulkan)
endif()

install(TARGETS medicaldisplay_v4l2
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/platform/linux
        FILES_MATCHING PATTERN "*.h")
endif()  # CMAKE_SYSTEM_NAME MATCHES "Linux"


