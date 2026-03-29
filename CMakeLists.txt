cmake_minimum_required(VERSION 3.10)
project(FlowerExchange CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${PROJECT_SOURCE_DIR}/gui/ui)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(lillyledger ${SOURCES})
target_include_directories(lillyledger PRIVATE ${PROJECT_SOURCE_DIR}/include)

set(BACKEND_SOURCES ${SOURCES})
list(FILTER BACKEND_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

set(GUI_SOURCES
    gui/main.cpp
    gui/mainwindow.cpp
    gui/import_export_panel.cpp
    gui/orderbook_widget.cpp
    gui/exec_reports_widget.cpp
    gui/order_entry_widget.cpp
    gui/performance_dashboard.cpp
    include/gui/mainwindow.h
    include/gui/import_export_panel.h
    include/gui/orderbook_widget.h
    include/gui/exec_reports_widget.h
    include/gui/order_entry_widget.h
    include/gui/performance_dashboard.h
    include/gui/qt_metatypes.h
    ${BACKEND_SOURCES}
)

add_executable(lillyledger-gui ${GUI_SOURCES})
target_include_directories(lillyledger-gui PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/gui
    ${PROJECT_SOURCE_DIR}/gui
)
target_link_libraries(lillyledger-gui PRIVATE Qt6::Widgets Qt6::Charts)

target_compile_options(lillyledger PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g -O0>
)
target_compile_options(lillyledger-gui PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g -O0>
)

install(TARGETS lillyledger-gui
    RUNTIME DESTINATION bin
)

install(TARGETS lillyledger
    RUNTIME DESTINATION bin
)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/gui/*.cpp"
        "${PROJECT_SOURCE_DIR}/include/gui/*.h"
        "${PROJECT_SOURCE_DIR}/include/*.h"
        "${PROJECT_SOURCE_DIR}/include/*.hpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Formatting source files with clang-format"
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMAT_SOURCES}
        COMMENT "Checking source formatting with clang-format"
    )
endif()
