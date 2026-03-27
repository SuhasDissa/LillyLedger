cmake_minimum_required(VERSION 3.10)
project(FlowerExchange CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(lillyledger ${SOURCES})
target_include_directories(lillyledger PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_compile_options(lillyledger PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g -O0>
)
