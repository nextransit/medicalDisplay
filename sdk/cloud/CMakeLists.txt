# Cloud Agent CMakeLists.txt
include(GNUInstallDirs)

add_library(medicaldisplay_cloud STATIC
    src/cloud_agent_impl.cpp
)

add_library(MedicalDisplay::Cloud ALIAS medicaldisplay_cloud)

target_include_directories(medicaldisplay_cloud PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

find_package(Threads REQUIRED)
target_link_libraries(medicaldisplay_cloud PUBLIC MedicalDisplay::Common Threads::Threads)

# MQTT (mosquitto)
find_package(PkgConfig QUIET)
pkg_check_modules(MOSQUITTO libmosquitto)
if(MOSQUITTO_FOUND)
    target_include_directories(medicaldisplay_cloud PUBLIC ${MOSQUITTO_INCLUDE_DIRS})
    target_link_libraries(medicaldisplay_cloud PUBLIC ${MOSQUITTO_LIBRARIES})
    target_compile_definitions(medicaldisplay_cloud PUBLIC HAS_MQTT)
endif()

# JSON (simdjson > nlohmann_json > cJSON)
# Prefer simdjson for fast parsing
find_path(SIMDJSON_INCLUDE_DIRS simdjson.h PATHS /opt/homebrew/include NO_DEFAULT_PATH)
find_library(SIMDJSON_LIBRARIES NAMES simdjson PATHS /opt/homebrew/lib NO_DEFAULT_PATH)
if(SIMDJSON_INCLUDE_DIRS AND SIMDJSON_LIBRARIES)
    target_include_directories(medicaldisplay_cloud PUBLIC ${SIMDJSON_INCLUDE_DIRS})
    target_link_libraries(medicaldisplay_cloud PUBLIC ${SIMDJSON_LIBRARIES})
    target_compile_definitions(medicaldisplay_cloud PUBLIC HAS_SIMDJSON)
else()
    # Fallback to nlohmann_json
    find_package(nlohmann_json QUIET)
    if(nlohmann_json_FOUND)
        target_link_libraries(medicaldisplay_cloud PUBLIC nlohmann_json::nlohmann_json)
        target_compile_definitions(medicaldisplay_cloud PUBLIC HAS_NLOHMANN_JSON)
    else()
        # Use cJSON as last resort fallback
        pkg_check_modules(CJSON QUIET libcjson cjson)
        if(CJSON_FOUND)
            target_include_directories(medicaldisplay_cloud PUBLIC ${CJSON_INCLUDE_DIRS})
            target_link_libraries(medicaldisplay_cloud PUBLIC ${CJSON_LIBRARIES})
            target_compile_definitions(medicaldisplay_cloud PUBLIC HAS_CJSON)
        endif()
    endif()
endif()

# CURL (for download / HTTP fallback)
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(medicaldisplay_cloud PUBLIC CURL::libcurl)
    target_compile_definitions(medicaldisplay_cloud PUBLIC HAS_CURL)
endif()

# OpenSSL (for HTTPS)
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(medicaldisplay_cloud PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(medicaldisplay_cloud PUBLIC HAS_OPENSSL)
endif()

install(TARGETS medicaldisplay_cloud
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/medicaldisplay/cloud
)
