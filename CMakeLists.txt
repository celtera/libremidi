cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
project(libremidi
  VERSION 4.1
  DESCRIPTION "A cross-platform MIDI library"
  LANGUAGES CXX
  HOMEPAGE_URL "https://github.com/jcelerier/libremidi"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CMakeDependentOption)
include(CheckSymbolExists)
include(CheckCXXSourceCompiles)
include(CheckIncludeFileCXX)

### Options ###
option(LIBREMIDI_HEADER_ONLY "Header-only mode" OFF)

cmake_dependent_option(LIBREMIDI_NO_COREMIDI "Disable CoreMidi back-end" OFF "APPLE" OFF)
cmake_dependent_option(LIBREMIDI_NO_WINMM "Disable WinMM back-end" OFF "WIN32" OFF)
cmake_dependent_option(LIBREMIDI_NO_WINUWP "Disable UWP back-end" ON "WIN32" OFF)
# if(LINUX) in CMake 3.25
cmake_dependent_option(LIBREMIDI_NO_ALSA "Disable ALSA back-end" OFF "UNIX; NOT APPLE" OFF)
cmake_dependent_option(LIBREMIDI_NO_UDEV "Disable udev support for ALSA" OFF "UNIX; NOT APPLE" OFF)
option(LIBREMIDI_NO_JACK "Disable JACK back-end" OFF)

option(LIBREMIDI_NO_EXPORTS "Disable dynamic symbol exporting" OFF)
option(LIBREMIDI_NO_BOOST "Do not use Boost if available" OFF)
option(LIBREMIDI_SLIM_MESSAGE "Use a fixed-size message format" 0)
option(LIBREMIDI_FIND_BOOST "Actively look for Boost" OFF)
option(LIBREMIDI_EXAMPLES "Enable examples" OFF)
option(LIBREMIDI_TESTS "Enable tests" OFF)
option(LIBREMIDI_CI "To be enabled only in CI, some tests cannot run there" OFF)

### C++ features ###
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
check_cxx_source_compiles("#include <thread>\nint main() { std::jthread t; }" HAS_STD_JTHREAD)


### Main library ###
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(LIBREMIDI_NO_BOOST AND LIBREMIDI_FIND_BOOST)
  message(FATAL_ERROR "LIBREMIDI_NO_BOOST and LIBREMIDI_FIND_BOOST are incompatible")
endif()

if(LIBREMIDI_FIND_BOOST)
  find_package(Boost REQUIRED)
endif()

if(LIBREMIDI_HEADER_ONLY)
  add_library(libremidi INTERFACE)
  set(_public INTERFACE)
  set(_private INTERFACE)
  target_compile_definitions(libremidi ${_public} LIBREMIDI_HEADER_ONLY)
else()
  add_library(libremidi
    include/libremidi/backends/alsa_seq/config.hpp
    include/libremidi/backends/alsa_seq/helpers.hpp
    include/libremidi/backends/alsa_seq/midi_in.hpp
    include/libremidi/backends/alsa_seq/midi_out.hpp
    include/libremidi/backends/alsa_seq/observer.hpp
    include/libremidi/backends/alsa_seq/shared_handler.hpp

    include/libremidi/backends/alsa_raw/config.hpp
    include/libremidi/backends/alsa_raw/helpers.hpp
    include/libremidi/backends/alsa_raw/midi_in.hpp
    include/libremidi/backends/alsa_raw/midi_out.hpp
    include/libremidi/backends/alsa_raw/observer.hpp

    include/libremidi/backends/alsa_raw_ump/config.hpp
    include/libremidi/backends/alsa_raw_ump/helpers.hpp
    include/libremidi/backends/alsa_raw_ump/midi_in.hpp
    include/libremidi/backends/alsa_raw_ump/midi_out.hpp
    include/libremidi/backends/alsa_raw_ump/observer.hpp

    include/libremidi/backends/alsa_seq_ump/config.hpp
    include/libremidi/backends/alsa_seq_ump/helpers.hpp
    include/libremidi/backends/alsa_seq_ump/midi_out.hpp

    include/libremidi/backends/coremidi/config.hpp
    include/libremidi/backends/coremidi/helpers.hpp
    include/libremidi/backends/coremidi/midi_in.hpp
    include/libremidi/backends/coremidi/midi_out.hpp
    include/libremidi/backends/coremidi/observer.hpp

    include/libremidi/backends/coremidi_ump/config.hpp
    include/libremidi/backends/coremidi_ump/helpers.hpp
    include/libremidi/backends/coremidi_ump/midi_in.hpp
    include/libremidi/backends/coremidi_ump/midi_out.hpp
    include/libremidi/backends/coremidi_ump/observer.hpp

    include/libremidi/backends/jack/config.hpp
    include/libremidi/backends/jack/helpers.hpp
    include/libremidi/backends/jack/midi_out.hpp
    include/libremidi/backends/jack/midi_in.hpp
    include/libremidi/backends/jack/observer.hpp
    include/libremidi/backends/jack/shared_handler.hpp

    include/libremidi/backends/linux/alsa.hpp
    include/libremidi/backends/linux/helpers.hpp
    include/libremidi/backends/linux/udev.hpp

    include/libremidi/backends/emscripten/config.hpp
    include/libremidi/backends/emscripten/helpers.hpp
    include/libremidi/backends/emscripten/midi_access.hpp
    include/libremidi/backends/emscripten/midi_access.cpp
    include/libremidi/backends/emscripten/midi_in.hpp
    include/libremidi/backends/emscripten/midi_in.cpp
    include/libremidi/backends/emscripten/midi_out.hpp
    include/libremidi/backends/emscripten/midi_out.cpp
    include/libremidi/backends/emscripten/observer.hpp
    include/libremidi/backends/emscripten/observer.cpp

    include/libremidi/backends/winmidi/config.hpp
    include/libremidi/backends/winmidi/helpers.hpp
    include/libremidi/backends/winmidi/midi_out.hpp
    include/libremidi/backends/winmidi/midi_in.hpp
    include/libremidi/backends/winmidi/observer.hpp

    include/libremidi/backends/winmm/config.hpp
    include/libremidi/backends/winmm/helpers.hpp
    include/libremidi/backends/winmm/midi_in.hpp
    include/libremidi/backends/winmm/midi_out.hpp
    include/libremidi/backends/winmm/observer.hpp

    include/libremidi/backends/winuwp/config.hpp
    include/libremidi/backends/winuwp/helpers.hpp
    include/libremidi/backends/winuwp/midi_out.hpp
    include/libremidi/backends/winuwp/midi_in.hpp
    include/libremidi/backends/winuwp/observer.hpp

    include/libremidi/backends/alsa_seq.hpp
    include/libremidi/backends/alsa_raw.hpp
    include/libremidi/backends/alsa_raw_ump.hpp
    include/libremidi/backends/coremidi.hpp
    include/libremidi/backends/coremidi_ump.hpp
    include/libremidi/backends/dummy.hpp
    include/libremidi/backends/emscripten.hpp
    include/libremidi/backends/jack.hpp
    include/libremidi/backends/winmm.hpp
    include/libremidi/backends/winuwp.hpp

    include/libremidi/detail/midi_api.hpp
    include/libremidi/detail/midi_in.hpp
    include/libremidi/detail/midi_out.hpp
    include/libremidi/detail/midi_stream_decoder.hpp
    include/libremidi/detail/observer.hpp

    include/libremidi/api.hpp
    include/libremidi/client.hpp
    include/libremidi/client.cpp
    include/libremidi/config.hpp
    include/libremidi/configurations.hpp
    include/libremidi/error.hpp
    include/libremidi/input_configuration.hpp
    include/libremidi/libremidi.hpp
    include/libremidi/message.hpp
    include/libremidi/output_configuration.hpp

    include/libremidi/reader.hpp
    include/libremidi/writer.hpp

    include/libremidi/libremidi.cpp
    include/libremidi/midi_in.cpp
    include/libremidi/midi_out.cpp
    include/libremidi/observer.cpp
    include/libremidi/reader.cpp
    include/libremidi/writer.cpp
  )
  set(_public PUBLIC)
  set(_private PRIVATE)
endif()

add_library(libremidi::libremidi ALIAS libremidi)

if(LIBREMIDI_SLIM_MESSAGE GREATER 0)
  target_compile_definitions(libremidi ${_public} LIBREMIDI_SLIM_MESSAGE=${LIBREMIDI_SLIM_MESSAGE})
endif()

if(LIBREMIDI_NO_BOOST)
  target_compile_definitions(libremidi ${_public} LIBREMIDI_NO_BOOST)
  message(STATUS "libremidi: Using std::vector for libremidi::message")
else()
  # Use of boost is public as it changes the ABI of libremidi::message
  if(TARGET Boost::boost)
    target_compile_definitions(libremidi ${_public} LIBREMIDI_USE_BOOST)
    target_link_libraries(libremidi ${_public} $<BUILD_INTERFACE:Boost::boost>)
    message(STATUS "libremidi: Using boost::small_vector for libremidi::message")
  elseif(Boost_INCLUDE_DIR)
    target_compile_definitions(libremidi ${_public} LIBREMIDI_USE_BOOST)
    target_include_directories(libremidi ${_public} $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>)
    message(STATUS "libremidi: Using boost::small_vector for libremidi::message")
  else()
    message(STATUS "libremidi: Using std::vector for libremidi::message")
  endif()
endif()

if(NOT LIBREMIDI_NO_EXPORTS)
  target_compile_definitions(libremidi ${_private} LIBREMIDI_EXPORTS)
endif()

target_compile_features(libremidi ${_public} cxx_std_20)

find_package(Threads)
target_link_libraries(libremidi ${_public} ${CMAKE_THREAD_LIBS_INIT})

target_include_directories(libremidi ${_public}
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(EMSCRIPTEN)
  message(STATUS "libremidi: using Emscripten MIDI")
  set(LIBREMIDI_HAS_EMSCRIPTEN 1)

  set(CMAKE_EXECUTABLE_SUFFIX .html)
  target_compile_definitions(libremidi ${_public} LIBREMIDI_EMSCRIPTEN)
  target_link_options(libremidi ${_public} "SHELL:-s 'EXPORTED_FUNCTIONS=[\"_main\", \"_free\", \"_libremidi_devices_poll\", \"_libremidi_devices_input\"]'")
elseif(APPLE)
  ## CoreMIDI support ##
  if(NOT LIBREMIDI_NO_COREMIDI)
    message(STATUS "libremidi: using CoreMIDI")
    set(LIBREMIDI_HAS_COREMIDI 1)

    target_compile_definitions(libremidi ${_public} LIBREMIDI_COREMIDI)

    find_library(COREMIDI_LIBRARY CoreMIDI)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)

    target_link_libraries(libremidi
      ${_public}
        ${COREFOUNDATION_LIBRARY}
        ${COREAUDIO_LIBRARY}
        ${COREMIDI_LIBRARY}
     )
  endif()

elseif(WIN32)
  ## WinMM support ##
  if(${CMAKE_SYSTEM_NAME} MATCHES WindowsStore)
    set(LIBREMIDI_NO_WINMM 1)
  endif()

  if(NOT LIBREMIDI_NO_WINMM)
    message(STATUS "libremidi: using WinMM")
    set(LIBREMIDI_HAS_WINMM 1)
    target_compile_definitions(libremidi
      ${_public}
        LIBREMIDI_WINMM
        UNICODE=1
        _UNICODE=1
    )
    target_link_libraries(libremidi ${_public} winmm)
  endif()

  ## UWP MIDI support ##
  if(NOT LIBREMIDI_NO_WINUWP)
    set(WINSDK_PATH "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]")
    find_path(CPPWINRT_PATH "winrt/base.h"
        PATHS
            ${WINSDK_PATH}
        PATH_SUFFIXES
            ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt
            Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt)
    if(CPPWINRT_PATH)
      message(STATUS "libremidi: using WinUWP")
      set(LIBREMIDI_HAS_WINUWP 1)

      target_include_directories(libremidi ${_public} "${CPPWINRT_PATH}")
      target_compile_definitions(libremidi ${_public} LIBREMIDI_WINUWP)
      target_link_libraries(libremidi INTERFACE RuntimeObject)
      # We don't need /ZW option here (support for C++/CX)' as we use C++/WinRT
      target_compile_options(libremidi ${_public} /EHsc /await)
    else()
      message(STATUS "libremidi: Failed to find Windows SDK, UWP MIDI backend will not be available")
    endif()
  endif()

elseif(UNIX AND NOT APPLE)
  ## ALSA support ##
  if(NOT LIBREMIDI_NO_ALSA)
    find_package(ALSA)
    check_include_file_cxx("sys/eventfd.h" LIBREMIDI_HAS_EVENTFD)
    check_include_file_cxx("sys/timerfd.h" LIBREMIDI_HAS_TIMERFD)

    if(ALSA_FOUND AND LIBREMIDI_HAS_EVENTFD AND LIBREMIDI_HAS_TIMERFD)
      message(STATUS "libremidi: using ALSA")
      set(LIBREMIDI_HAS_ALSA 1)

      target_compile_definitions(libremidi ${_public} LIBREMIDI_ALSA)

      if(NOT LIBREMIDI_NO_UDEV)
        find_path(UDEV_INCLUDE_DIR libudev.h)
        find_library(UDEV_LIBRARY udev)
        if(UDEV_INCLUDE_DIR AND UDEV_LIBRARY)
          target_compile_definitions(libremidi ${_public} LIBREMIDI_HAS_UDEV)
          target_include_directories(libremidi ${_public} ${UDEV_INCLUDE_DIR})
          target_link_libraries(libremidi ${_public} ${UDEV_LIBRARY})
        endif()
      endif()
    endif()
  endif()
endif()

## JACK support ##
if(NOT LIBREMIDI_NO_JACK)
  find_path(WEAKJACK_PATH weakjack/weak_libjack.h HINTS ${WEAKJACK_FOLDER})
  find_path(JACK_PATH jack/jack.h)
  if(WEAKJACK_PATH AND JACK_PATH)
    message(STATUS "libremidi: using WeakJACK")
    set(LIBREMIDI_HAS_JACK 1)

    target_include_directories(libremidi ${_public} $<BUILD_INTERFACE:${WEAKJACK_PATH}> $<BUILD_INTERFACE:${JACK_PATH}>)
  elseif(JACK_PATH)
    find_library(JACK_LIBRARIES jack)
    if(JACK_LIBRARIES)
      message(STATUS "libremidi: using linked JACK")
      set(LIBREMIDI_HAS_JACK 1)
      
      target_link_libraries(libremidi ${_public} ${JACK_LIBRARIES})
      target_include_directories(libremidi ${_public} $<BUILD_INTERFACE:${JACK_PATH}>)
    endif()
  endif()

  if(LIBREMIDI_HAS_JACK)
    target_compile_definitions(libremidi ${_public} LIBREMIDI_JACK)
  endif()
endif()

### Install ###
include(libremidi.install)

### Examples ###
if(LIBREMIDI_EXAMPLES)
  message(STATUS "libremidi: compiling examples")
  include(libremidi.examples)
endif()

### Tests ###
if(LIBREMIDI_TESTS)
  include(libremidi.tests)
endif()
