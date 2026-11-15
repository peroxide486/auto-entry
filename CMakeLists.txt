cmake_minimum_required(VERSION 3.24)
project(auto-entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(externals/robot-cpp)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(auto_entry ${SOURCES})

target_link_libraries(auto_entry PRIVATE robot::robot)

if(MSVC)
    target_compile_options(auto_entry PRIVATE /utf-8)
endif()
