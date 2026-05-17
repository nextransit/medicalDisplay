cmake_minimum_required(VERSION 3.16)
project(MedicalDisplayApp VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# macOS: 修复 Command Line Tools C++ 头文件路径问题
if(APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -cxx-isystem ${SDK_PATH}/usr/include/c++/v1")
endif()

# ---- Qt 配置 ----
# 优先使用 Qt6，回退到 Qt5
find_package(Qt6 COMPONENTS Quick QuickControls2 Widgets Concurrent QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Quick QuickControls2 Widgets Concurrent)
    set(QT_VERSION_MAJOR 5)
    message(STATUS "使用 Qt5: ${Qt5_VERSION}")
else()
    set(QT_VERSION_MAJOR 6)
    message(STATUS "使用 Qt6: ${Qt6_VERSION}")
endif()

# ---- 源文件 ----
set(APP_SOURCES
    src/main.cpp
    src/imagerenderer.cpp
)

# macOS Metal 后端
if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(KIT_FRAMEWORK MetalKit REQUIRED)
    list(APPEND APP_SOURCES src/metal_backend.mm)
    set_source_files_properties(src/metal_backend.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND APP_SOURCES src/opengl_backend.cpp)
    find_package(OpenGL REQUIRED)
endif()

# QML 资源
set(QML_RESOURCES resources/qml.qrc)

# ---- 可执行文件 ----
if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(MedicalDisplayApp ${APP_SOURCES} ${QML_RESOURCES})
else()
    add_executable(MedicalDisplayApp ${APP_SOURCES} ${QML_RESOURCES})
    target_link_libraries(MedicalDisplayApp PRIVATE
        Qt5::Quick
        Qt5::QuickControls2
        Qt5::Widgets
        Qt5::Concurrent
    )
endif()

# 链接库
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(MedicalDisplayApp PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        Qt6::Concurrent
    )
endif()

if(APPLE)
    target_link_libraries(MedicalDisplayApp PRIVATE ${METAL_FRAMEWORK} ${KIT_FRAMEWORK})
else()
    target_link_libraries(MedicalDisplayApp PRIVATE OpenGL::GL)
endif()

# macOS app bundle
if(APPLE)
    set_target_properties(MedicalDisplayApp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.medicaldisplay.ai"
        MACOSX_BUNDLE_BUNDLE_NAME "AI Medical Display"
        MACOSX_BUNDLE_BUNDLE_VERSION "2.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "2.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
endif()

# 安装
install(TARGETS MedicalDisplayApp
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
