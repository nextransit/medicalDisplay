# Predictive Maintenance Module
add_library(medicaldisplay_predictive STATIC
    src/predictive_maintenance.cpp
)

target_include_directories(medicaldisplay_predictive PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_predictive PUBLIC cxx_std_17)

target_link_libraries(medicaldisplay_predictive PUBLIC
    Threads::Threads
)

if(MSVC)
    target_compile_options(medicaldisplay_predictive PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_predictive PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_predictive
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/predictive
        FILES_MATCHING PATTERN "*.h")
