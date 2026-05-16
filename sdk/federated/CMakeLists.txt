# Federated Learning Client Module
add_library(medicaldisplay_federated STATIC
    src/federated_client.cpp
)

target_include_directories(medicaldisplay_federated PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_federated PUBLIC cxx_std_17)

target_link_libraries(medicaldisplay_federated PUBLIC
    Threads::Threads
)

if(MSVC)
    target_compile_options(medicaldisplay_federated PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_federated PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_federated
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/federated
        FILES_MATCHING PATTERN "*.h")
