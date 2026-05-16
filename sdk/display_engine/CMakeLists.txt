# Display Engine CMakeLists.txt
add_library(medicaldisplay_display STATIC
    src/display_engine_impl.cpp
)

add_library(MedicalDisplay::Display ALIAS medicaldisplay_display)

target_include_directories(medicaldisplay_display PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Vulkan
find_package(Vulkan QUIET)
target_link_libraries(medicaldisplay_display PUBLIC
    medicaldisplay_dicom
    medicaldisplay_ai
    Threads::Threads
)

if(Vulkan_FOUND)
    target_link_libraries(medicaldisplay_display PUBLIC Vulkan::Vulkan)
endif()

# DRM (Linux)
if(UNIX AND NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DRM QUIET libdrm)
    endif()
    if(DRM_FOUND)
        target_include_directories(medicaldisplay_display PUBLIC ${DRM_INCLUDE_DIRS})
        target_link_libraries(medicaldisplay_display PUBLIC ${DRM_LIBRARIES})
        target_compile_definitions(medicaldisplay_display PUBLIC HAS_LIBDRM)
    endif()
endif()

# Installation
install(TARGETS medicaldisplay_display
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/display
)
