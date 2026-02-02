cmake_minimum_required(VERSION 3.16)
project(Findex)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(findex_core STATIC
    core/appdata.cpp
    core/elevate.cpp
    core/indexer.cpp
    core/findex.cpp
    core/count_volumes.cpp
)

target_include_directories(findex_core PUBLIC core/include)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(GUI_HEADERS
    gui/mainwindow.h
)
set(GUI_SOURCES
    gui/mainwindow.cpp
    gui/main.cpp
)

qt_wrap_cpp(MOC_SOURCES
    ${GUI_HEADERS}
)

qt_add_executable(findex_gui
    ${GUI_SOURCES}
    ${MOC_SOURCES}
)

target_link_libraries(findex_gui PRIVATE findex_core Qt6::Widgets)
target_include_directories(findex_gui PRIVATE core/include gui)