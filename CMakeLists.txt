cmake_minimum_required(VERSION 3.9 FATAL_ERROR)
project(libremidi CXX)

option(REMIDI_HEADER_ONLY "Header-only mode" OFF)
option(REMIDI_NO_COREMIDI "Disable CoreMidi back-end" OFF)
option(REMIDI_NO_WINMM "Disable WinMM back-end" OFF)
option(REMIDI_NO_WINUWP "Disable UWP back-end" ON)
option(REMIDI_NO_JACK "Disable JACK back-end" OFF)
option(REMIDI_NO_ALSA "Disable ALSA back-end" OFF)
option(REMIDI_EXAMPLES "Enable examples" ON)

include(CheckSymbolExists)
### Main library ###
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(REMIDI_HEADER_ONLY)
  add_library(remidi INTERFACE)
  set(_public INTERFACE)
  target_compile_definitions(remidi ${_public} REMIDI_HEADER_ONLY)
else()
  add_library(remidi
    include/remidi/detail/alsa.hpp
    include/remidi/detail/coreaudio.hpp
    include/remidi/detail/dummy.hpp
    include/remidi/detail/jack.hpp
    include/remidi/detail/midi_api.hpp
    include/remidi/detail/semaphore.hpp
    include/remidi/detail/winmm.hpp
    include/remidi/detail/winuwp.hpp

    include/remidi/message.hpp
    include/remidi/reader.hpp
    include/remidi/writer.hpp
    include/remidi/remidi.hpp

    include/remidi/remidi.cpp
    include/remidi/reader.cpp
    include/remidi/writer.cpp
  )
  set(_public PUBLIC)
endif()

target_compile_features(remidi ${_public} cxx_std_17)

find_package(Threads)
target_link_libraries(remidi ${_public} ${CMAKE_THREAD_LIBS_INIT})

target_include_directories(remidi ${_public}
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(APPLE)
  ## CoreMIDI support ##
  if(NOT REMIDI_NO_COREMIDI)
    target_compile_definitions(remidi ${_public} REMIDI_COREAUDIO)

    find_library(COREMIDI_LIBRARY CoreMIDI)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)

    target_link_libraries(remidi
      ${_public}
        ${COREFOUNDATION_LIBRARY}
        ${COREAUDIO_LIBRARY}
        ${COREMIDI_LIBRARY}
     )
  endif()

elseif(WIN32)
  ## WinMM support ##
  if(NOT REMIDI_NO_WINMM)
    target_compile_definitions(remidi
      ${_public}
        REMIDI_WINMM
        UNICODE=1
        _UNICODE=1
    )
    target_link_libraries(remidi ${_public} winmm)
  endif()

  ## UWP MIDI support ##
  if(NOT REMIDI_NO_WINUWP)
    set(WINSDK_PATH "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]")
    find_path(CPPWINRT_PATH "winrt/base.h"
        PATHS
            ${WINSDK_PATH}
        PATH_SUFFIXES
            ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt
            Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt)
    if(CPPWINRT_PATH)
      target_include_directories(remidi ${_public} "${CPPWINRT_PATH}")
      target_compile_definitions(remidi ${_public} REMIDI_WINUWP)
      target_link_libraries(remidi INTERFACE RuntimeObject)
      # We don't need /ZW option here (support for C++/CX)' as we use C++/WinRT
      target_compile_options(remidi ${_public} /EHsc /await)
    else()
      message("Failed to find Windows SDK, UWP MIDI backend will not be available")
    endif()
  endif()

elseif(UNIX)
  ## ALSA support ##
  if(NOT REMIDI_NO_ALSA)
    find_package(ALSA)
    if(ALSA_FOUND)
      target_compile_definitions(remidi ${_public} REMIDI_ALSA)
      target_link_libraries(remidi ${_public} ${ALSA_LIBRARIES})
    endif()
  endif()
endif()

## JACK support ##
if(NOT REMIDI_NO_JACK)
  find_path(WEAKJACK_PATH weakjack/weak_libjack.h HINTS ${WEAKJACK_FOLDER})
  find_path(JACK_PATH jack/jack.h)
  if(WEAKJACK_PATH AND JACK_PATH)
    message(" -- remidi : Using WeakJACK")
    set(HAS_JACK 1)
    target_include_directories(remidi ${_public} $<BUILD_INTERFACE:${WEAKJACK_PATH}> $<BUILD_INTERFACE:${JACK_PATH}>)
  elseif(JACK_PATH)
    message(" -- remidi : Using linked JACK")
    find_library(JACK_LIBRARIES jack)
    if(JACK_LIBRARIES)
      set(HAS_JACK 1)
      target_link_libraries(remidi ${_public} ${JACK_LIBRARIES})
      target_include_directories(remidi ${_public} $<BUILD_INTERFACE:${JACK_PATH}>)
    endif()
  endif()

  if(HAS_JACK)
    target_compile_definitions(remidi ${_public} REMIDI_JACK)

    check_symbol_exists(jack_port_rename jack/jack.h HAS_JACK_PORT_RENAME)
    if(HAS_JACK_PORT_RENAME)
      target_compile_definitions(remidi ${_public} REMIDI_JACK_HAS_PORT_RENAME)
    endif()
  endif()
endif()

### Install  ###
if(NOT REMIDI_HEADER_ONLY)
  install(TARGETS remidi
          EXPORT remidi-exports
          ARCHIVE DESTINATION lib/static
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          )
else()
    install(TARGETS remidi
            EXPORT remidi-exports
            )
endif()
install(EXPORT remidi-exports
        DESTINATION lib/cmake/remidi)
export(EXPORT remidi-exports)

### Examples ###
if(REMIDI_EXAMPLES)
  add_executable(midiobserve tests/midiobserve.cpp)
  target_link_libraries(midiobserve PRIVATE remidi)

  add_executable(cmidiin tests/cmidiin.cpp)
  target_link_libraries(cmidiin PRIVATE remidi)

  add_executable(midiclock_in tests/midiclock_in.cpp)
  target_link_libraries(midiclock_in PRIVATE remidi)

  add_executable(midiclock_out tests/midiclock_out.cpp)
  target_link_libraries(midiclock_out PRIVATE remidi)

  add_executable(midiout tests/midiout.cpp)
  target_link_libraries(midiout PRIVATE remidi)

  add_executable(midiprobe tests/midiprobe.cpp)
  target_link_libraries(midiprobe PRIVATE remidi)

  add_executable(qmidiin tests/qmidiin.cpp)
  target_link_libraries(qmidiin PRIVATE remidi)

  add_executable(sysextest tests/sysextest.cpp)
  target_link_libraries(sysextest PRIVATE remidi)

  add_executable(midi2 tests/midi2.cpp)
  target_link_libraries(midi2 PRIVATE remidi)
endif()

### Tests ###
find_package(Catch2)
if(TARGET Catch2::Catch2)
  target_compile_features(remidi PUBLIC cxx_std_20)

  add_executable(midiout_test tests/unit/midi_out.cpp)
  target_link_libraries(midiout_test PRIVATE remidi Catch2::Catch2)

  add_executable(midifile_read_test tests/unit/midifile_read.cpp)
  target_link_libraries(midifile_read_test PRIVATE remidi Catch2::Catch2)
  target_compile_definitions(midifile_read_test PRIVATE "REMIDI_TEST_CORPUS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus\"")

  include(CTest)
  include(Catch)
  catch_discover_tests(midiout_test)
  catch_discover_tests(midifile_read_test)
endif()
