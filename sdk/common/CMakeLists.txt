# Common utilities CMakeLists.txt
add_library(medicaldisplay_common INTERFACE)
add_library(MedicalDisplay::Common ALIAS medicaldisplay_common)

target_include_directories(medicaldisplay_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Platform detection
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(medicaldisplay_common INTERFACE PLATFORM_LINUX)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_compile_definitions(medicaldisplay_common INTERFACE PLATFORM_MACOS)
elseif(ANDROID)
    target_compile_definitions(medicaldisplay_common INTERFACE PLATFORM_ANDROID)
else()
    target_compile_definitions(medicaldisplay_common INTERFACE PLATFORM_LINUX)
endif()

# Highway SIMD support
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HWY QUIET libhwy)
endif()

if(HWY_FOUND)
    message(STATUS "Highway SIMD library found: ${HWY_VERSION}")
    target_compile_definitions(medicaldisplay_common INTERFACE HIGHWAY_ENABLED)
    target_include_directories(medicaldisplay_common INTERFACE ${HWY_INCLUDE_DIRS})
    target_link_libraries(medicaldisplay_common INTERFACE ${HWY_LIBRARIES})
else()
    # Try cmake find_package as fallback
    find_library(HWY_LIBRARY NAMES hwy PATHS /opt/homebrew/lib /usr/local/lib)
    if(HWY_LIBRARY)
        message(STATUS "Highway library found: ${HWY_LIBRARY}")
        target_compile_definitions(medicaldisplay_common INTERFACE HIGHWAY_ENABLED)
        target_include_directories(medicaldisplay_common INTERFACE /opt/homebrew/include)
        target_link_libraries(medicaldisplay_common INTERFACE ${HWY_LIBRARY})
    else()
        message(STATUS "Highway SIMD library not found, using scalar fallbacks")
    endif()
endif()

# SIMD utilities sources (only compiled if Highway is available)
add_library(medicaldisplay_simd STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/simd_utils.cpp
)
target_include_directories(medicaldisplay_simd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(medicaldisplay_simd PRIVATE MedicalDisplay::Common)
if(HWY_FOUND OR HWY_LIBRARY)
    target_compile_definitions(medicaldisplay_simd PUBLIC HIGHWAY_ENABLED)
    target_include_directories(medicaldisplay_simd PUBLIC ${HWY_INCLUDE_DIRS})
    target_link_directories(medicaldisplay_simd PUBLIC ${HWY_LIBRARY_DIRS})
    target_link_libraries(medicaldisplay_simd PUBLIC ${HWY_LIBRARIES} ${HWY_LIBRARY})
endif()
