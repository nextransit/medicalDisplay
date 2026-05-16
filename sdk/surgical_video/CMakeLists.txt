# Surgical Video Enhancement Module
add_library(medicaldisplay_surgical STATIC
    src/surgical_video.cpp
)

target_include_directories(medicaldisplay_surgical PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_surgical PUBLIC cxx_std_17)

target_link_libraries(medicaldisplay_surgical PUBLIC
    Threads::Threads
)

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
