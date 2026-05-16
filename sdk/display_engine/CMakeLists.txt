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
    medicaldisplay_simd
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

# Shader compilation (optional glslangValidator)
find_program(GLSLANGVALIDATOR NAMES glslangValidator glslang)
if(GLSLANGVALIDATOR)
    message(STATUS "Found glslangValidator: ${GLSLANGVALIDATOR}")

    set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    set(SPIRV_DIR "${CMAKE_BINARY_DIR}/shaders")

    file(MAKE_DIRECTORY ${SPIRV_DIR})

    set(COMP_SHADERS bitdepth_convert.comp)

    foreach(SHADER ${COMP_SHADERS})
        set(SPIRV_FILE "${SPIRV_DIR}/${SHADER}.spv")
        add_custom_command(
            OUTPUT ${SPIRV_FILE}
            COMMAND ${GLSLANGVALIDATOR} -V100 -x -o ${SPIRV_FILE} ${SHADER_DIR}/${SHADER}
            DEPENDS ${SHADER_DIR}/${SHADER}
            COMMENT "Compiling ${SHADER} to SPIR-V"
            VERBATIM
        )
        list(APPEND SPIRV_OUTPUTS ${SPIRV_FILE})
    endforeach()

    add_custom_target(compile_shaders ALL DEPENDS ${SPIRV_OUTPUTS})
else()
    message(STATUS "glslangValidator not found - shader compilation disabled")
endif()

# Installation
install(TARGETS medicaldisplay_display
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/display
)
