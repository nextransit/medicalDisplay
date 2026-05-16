# Digital Twin Module
add_library(medicaldisplay_digital_twin STATIC
    src/digital_twin.cpp
)

target_include_directories(medicaldisplay_digital_twin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_digital_twin PUBLIC cxx_std_17)

target_link_libraries(medicaldisplay_digital_twin PUBLIC
    Threads::Threads
)

if(MSVC)
    target_compile_options(medicaldisplay_digital_twin PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_digital_twin PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_digital_twin
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/digital_twin
        FILES_MATCHING PATTERN "*.h")
