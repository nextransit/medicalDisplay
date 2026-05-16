# Multimodal Fusion Module
add_library(medicaldisplay_multimodal STATIC
    src/multimodal_fusion.cpp
)

target_include_directories(medicaldisplay_multimodal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_multimodal PUBLIC cxx_std_17)

target_link_libraries(medicaldisplay_multimodal PUBLIC
    Threads::Threads
)

if(MSVC)
    target_compile_options(medicaldisplay_multimodal PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_multimodal PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_multimodal
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/multimodal
        FILES_MATCHING PATTERN "*.h")
