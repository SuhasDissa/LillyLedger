cmake_minimum_required(VERSION 3.14)
project(LillyLedgerTests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(PARENT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(PARENT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)

file(GLOB_RECURSE PARENT_SOURCES ${PARENT_SRC_DIR}/*.cpp)
list(FILTER PARENT_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES unit/*.cpp integration/*.cpp)
foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} ${PARENT_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE ${PARENT_INCLUDE_DIR})
    target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(${TEST_NAME} GTest::gtest_main)
    gtest_discover_tests(${TEST_NAME})
endforeach()
