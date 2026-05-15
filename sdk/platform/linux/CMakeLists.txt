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
