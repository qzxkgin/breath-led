cmake_minimum_required(VERSION 3.16)
project(breath_led LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(breath_led vision/main.cpp)
target_link_libraries(breath_led PRIVATE Threads::Threads)

if(UNIX)
    add_executable(breath_led_linux vision/main_linux.cpp)
    target_link_libraries(breath_led_linux PRIVATE Threads::Threads)
endif()
