# Performance Optimization Module
add_library(medicaldisplay_performance STATIC
    src/memory_pool.cpp
    src/simd_processing.cpp
)

target_include_directories(medicaldisplay_performance PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(medicaldisplay_performance PUBLIC cxx_std_17)

# SIMD flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Enable AVX2/SSE4.2
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        target_compile_options(medicaldisplay_performance PRIVATE -mavx2 -msse4.2)
    endif()
endif()

target_link_libraries(medicaldisplay_performance PUBLIC
    Threads::Threads
)

find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    target_link_libraries(medicaldisplay_performance PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(medicaldisplay_performance PUBLIC MEDICALDISPLAY_OPENMP_ENABLED)
endif()

if(MSVC)
    target_compile_options(medicaldisplay_performance PRIVATE /W4)
else()
    target_compile_options(medicaldisplay_performance PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS medicaldisplay_performance
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/performance
        FILES_MATCHING PATTERN "*.h")
