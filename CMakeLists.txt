cmake_minimum_required(VERSION 3.16)
project(Findex CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(findex_core STATIC
    core/appdata.cpp
    core/elevate.cpp
    core/indexer.cpp
    core/findex.cpp
    core/count_volumes.cpp
    core/wstring_to_utf8.cpp
)

target_include_directories(findex_core PUBLIC core/include)
target_compile_definitions(findex_core PUBLIC NOMINMAX UNICODE _UNICODE)
target_link_libraries(findex_core PUBLIC shell32 shlwapi advapi32 user32 ole32 oleaut32 uuid)
if(MSVC)
    target_compile_options(findex_core PUBLIC /utf-8 /W4 /permissive-)
endif()

add_executable(findex
    cli/main.cpp
    cli/term.cpp
    cli/tui.cpp
    cli/shell.cpp
    cli/source.cpp
    cli/service.cpp
)
target_include_directories(findex PRIVATE cli)
target_link_libraries(findex PRIVATE findex_core)
