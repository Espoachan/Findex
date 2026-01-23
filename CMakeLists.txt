cmake_minimum_required(VERSION 3.16)
project(Findex)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(core/include)

add_executable(findex
    core/main.cpp
    core/appdatapath.cpp
    core/makeadministrator.cpp
    core/indexer.cpp
)
