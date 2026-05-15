cmake_minimum_required(VERSION 3.22.1)
project(medicaldisplay_demo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(medicaldisplay_demo SHARED native-lib.cpp)

find_library(log-lib log)
target_link_libraries(medicaldisplay_demo PRIVATE ${log-lib})
