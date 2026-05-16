# AR Overlay Module
add_library(medicaldisplay_ar_overlay STATIC
    src/ar_overlay.cpp
)

target_include_directories(medicaldisplay_ar_overlay PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_ar_overlay PUBLIC cxx_std_17)

target_link_libraries(medicaldisplay_ar_overlay PUBLIC
    Threads::Threads
)

if(MSVC)
    target_compile_options(medicaldisplay_ar_overlay PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_ar_overlay PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_ar_overlay
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/ar_overlay
        FILES_MATCHING PATTERN "*.h")
