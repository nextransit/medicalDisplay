# Ambient Light Sensor Module
add_library(medicaldisplay_ambient_light STATIC
    src/ambient_light_sensor.cpp
)

target_include_directories(medicaldisplay_ambient_light PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_ambient_light PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(medicaldisplay_ambient_light PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_ambient_light PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_ambient_light
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/ambient_light
        FILES_MATCHING PATTERN "*.h")
