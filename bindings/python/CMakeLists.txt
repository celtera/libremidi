cmake_minimum_required(VERSION 3.28 FATAL_ERROR)
project(pylibremidi)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL 1)

set(LIBREMIDI_PYTHON 1)
set(LIBREMIDI_FIND_BOOST 1)
set(LIBREMIDI_HEADER_ONLY 1)
set(LIBREMIDI_NEEDS_READERWRITERQUEUE 1)

add_subdirectory(../.. libremidi-src)

find_package(Python 3.10 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(Python 3.10 COMPONENTS Development.SABIModule QUIET)

if(NOT readerwriterqueue_FOUND AND NOT TARGET readerwriterqueue)
    FetchContent_Declare(
            readerwriterqueue
            GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue
            GIT_TAG        master
    )

    FetchContent_MakeAvailable(readerwriterqueue)
endif()

FetchContent_Declare(
    nanobind
    GIT_REPOSITORY https://github.com/wjakob/nanobind
    GIT_TAG        master
)

FetchContent_MakeAvailable(nanobind)

if(Python_VERSION VERSION_GREATER_EQUAL "3.12")
    nanobind_add_module(pylibremidi STABLE_ABI NB_STATIC pylibremidi.cpp)
else()
    nanobind_add_module(pylibremidi NB_STATIC pylibremidi.cpp)
endif()
target_link_libraries(pylibremidi PUBLIC libremidi readerwriterqueue Boost::headers Boost::variant2 Boost::container)

nanobind_add_stub(
    pylibremidi_stub
    MODULE pylibremidi
    OUTPUT pylibremidi.pyi
    PYTHON_PATH $<TARGET_FILE_DIR:pylibremidi>
    DEPENDS pylibremidi
)

install(TARGETS pylibremidi LIBRARY DESTINATION .)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/pylibremidi.pyi
    DESTINATION .
    OPTIONAL
)
