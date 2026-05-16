# Multi-Display Coordination Module
add_library(medicaldisplay_multiscreen STATIC
    src/multi_display_hub.cpp
)

target_include_directories(medicaldisplay_multiscreen PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_multiscreen PUBLIC cxx_std_17)

target_link_libraries(medicaldisplay_multiscreen PUBLIC
    Threads::Threads
)

if(MSVC)
    target_compile_options(medicaldisplay_multiscreen PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_multiscreen PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_multiscreen
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/multiscreen
        FILES_MATCHING PATTERN "*.h")
