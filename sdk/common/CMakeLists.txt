# Common utilities CMakeLists.txt
add_library(medicaldisplay_common INTERFACE)
add_library(MedicalDisplay::Common ALIAS medicaldisplay_common)

target_include_directories(medicaldisplay_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Platform detection
if(UNIX AND NOT ANDROID)
    target_compile_definitions(medicaldisplay_common INTERFACE PLATFORM_LINUX)
elseif(ANDROID)
    target_compile_definitions(medicaldisplay_common INTERFACE PLATFORM_ANDROID)
endif()
