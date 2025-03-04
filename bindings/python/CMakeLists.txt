cmake_minimum_required(VERSION 3.28 FATAL_ERROR)
project(pylibremidi)

set(LIBREMIDI_FIND_BOOST 1)
set(LIBREMIDI_HEADER_ONLY 1)
set(LIBREMIDI_NEEDS_READERWRITERQUEUE 1)
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL 1)
add_subdirectory(../.. libremidi-src)
find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)

FetchContent_Declare(
    nanobind
    GIT_REPOSITORY https://github.com/wjakob/nanobind
    GIT_TAG        master
)

FetchContent_MakeAvailable(nanobind)
nanobind_add_module(pylibremidi pylibremidi.cpp)
target_link_libraries(pylibremidi PUBLIC libremidi readerwriterqueue)
