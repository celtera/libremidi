cmake_minimum_required(VERSION 3.9 FATAL_ERROR)
project(libremidi 
  VERSION 3.0
  DESCRIPTION "A cross-platform MIDI library"
  LANGUAGES CXX
  HOMEPAGE_URL "https://github.com/jcelerier/libremidi"
)

option(LIBREMIDI_HEADER_ONLY "Header-only mode" OFF)
option(LIBREMIDI_NO_COREMIDI "Disable CoreMidi back-end" OFF)
option(LIBREMIDI_NO_WINMM "Disable WinMM back-end" OFF)
option(LIBREMIDI_NO_WINUWP "Disable UWP back-end" ON)
option(LIBREMIDI_NO_JACK "Disable JACK back-end" OFF)
option(LIBREMIDI_NO_ALSA "Disable ALSA back-end" OFF)
option(LIBREMIDI_NO_EXPORTS "Disable dynamic symbol exporting" OFF)
option(LIBREMIDI_NO_BOOST "Do not use Boost if available" OFF)
option(LIBREMIDI_FIND_BOOST "Actively look for Boost" OFF)
option(LIBREMIDI_EXAMPLES "Enable examples" OFF)
option(LIBREMIDI_TESTS "Enable tests" ON)

include(CheckSymbolExists)
include(CheckCXXSourceCompiles)

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
    include/libremidi/detail/alsa.hpp
    include/libremidi/detail/coreaudio.hpp
    include/libremidi/detail/dummy.hpp
    include/libremidi/detail/emscripten.hpp
    include/libremidi/detail/emscripten_api.hpp
    include/libremidi/detail/jack.hpp
    include/libremidi/detail/midi_api.hpp
    include/libremidi/detail/raw_alsa.hpp
    include/libremidi/detail/raw_alsa_helpers.hpp
    include/libremidi/detail/semaphore.hpp
    include/libremidi/detail/winmm.hpp
    include/libremidi/detail/winuwp.hpp

    include/libremidi/config.hpp
    include/libremidi/message.hpp
    include/libremidi/reader.hpp
    include/libremidi/writer.hpp
    include/libremidi/libremidi.hpp

    include/libremidi/libremidi.cpp
    include/libremidi/reader.cpp
    include/libremidi/writer.cpp
  )
  set(_public PUBLIC)
  set(_private PRIVATE)
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

target_compile_features(libremidi ${_public} cxx_std_17)

find_package(Threads)
target_link_libraries(libremidi ${_public} ${CMAKE_THREAD_LIBS_INIT})

target_include_directories(libremidi ${_public}
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(EMSCRIPTEN)
  message(STATUS "libremidi: using Emscripten MIDI")
  set(CMAKE_EXECUTABLE_SUFFIX .html)
  target_compile_definitions(libremidi ${_public} LIBREMIDI_EMSCRIPTEN)
elseif(APPLE)
  ## CoreMIDI support ##
  if(NOT LIBREMIDI_NO_COREMIDI)
    message(STATUS "libremidi: using CoreMIDI")
    target_compile_definitions(libremidi ${_public} LIBREMIDI_COREAUDIO)

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
  if(NOT LIBREMIDI_NO_WINMM)
    message(STATUS "libremidi: using WinMM")
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
      target_include_directories(libremidi ${_public} "${CPPWINRT_PATH}")
      target_compile_definitions(libremidi ${_public} LIBREMIDI_WINUWP)
      target_link_libraries(libremidi INTERFACE RuntimeObject)
      # We don't need /ZW option here (support for C++/CX)' as we use C++/WinRT
      target_compile_options(libremidi ${_public} /EHsc /await)
    else()
      message(STATUS "libremidi: Failed to find Windows SDK, UWP MIDI backend will not be available")
    endif()
  endif()

elseif(UNIX)
  ## ALSA support ##
  if(NOT LIBREMIDI_NO_ALSA)
    find_package(ALSA)
    if(ALSA_FOUND)
      message(STATUS "libremidi: using ALSA")
      target_compile_definitions(libremidi ${_public} LIBREMIDI_ALSA)
      target_link_libraries(libremidi ${_public} ${ALSA_LIBRARIES})
    endif()
  endif()
endif()

## JACK support ##
if(NOT LIBREMIDI_NO_JACK)
  find_path(WEAKJACK_PATH weakjack/weak_libjack.h HINTS ${WEAKJACK_FOLDER})
  find_path(JACK_PATH jack/jack.h)
  if(WEAKJACK_PATH AND JACK_PATH)
    message(STATUS "libremidi: using WeakJACK")
    set(HAS_JACK 1)
    target_include_directories(libremidi ${_public} $<BUILD_INTERFACE:${WEAKJACK_PATH}> $<BUILD_INTERFACE:${JACK_PATH}>)
  elseif(JACK_PATH)
    message(STATUS "libremidi: using linked JACK")
    find_library(JACK_LIBRARIES jack)
    if(JACK_LIBRARIES)
      set(HAS_JACK 1)
      target_link_libraries(libremidi ${_public} ${JACK_LIBRARIES})
      target_include_directories(libremidi ${_public} $<BUILD_INTERFACE:${JACK_PATH}>)
    endif()
  endif()

  if(HAS_JACK)
    target_compile_definitions(libremidi ${_public} LIBREMIDI_JACK)

    check_symbol_exists(jack_port_rename jack/jack.h HAS_JACK_PORT_RENAME)
    if(HAS_JACK_PORT_RENAME)
      target_compile_definitions(libremidi ${_public} LIBREMIDI_JACK_HAS_PORT_RENAME)
    endif()
  endif()
endif()

### Install  ###
if(NOT LIBREMIDI_HEADER_ONLY)
  install(TARGETS libremidi
          EXPORT libremidi-targets
          ARCHIVE DESTINATION lib/static
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
  )
else()
  install(TARGETS libremidi
          EXPORT libremidi-targets
  )
endif()
install(EXPORT libremidi-targets
        DESTINATION lib/cmake/libremidi)
install(DIRECTORY include
        DESTINATION .)
export(EXPORT libremidi-targets)

include(CMakePackageConfigHelpers)

# generate the config file that includes the exports
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libremidi-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/libremidi-config.cmake"
  INSTALL_DESTINATION "lib/cmake/libremidi"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/libremidi-config-version.cmake"
  VERSION "${CMAKE_PROJECT_VERSION}"
  COMPATIBILITY AnyNewerVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libremidi-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/libremidi-config-version.cmake
  DESTINATION lib/cmake/libremidi
)

### Examples ###
if(LIBREMIDI_EXAMPLES)
  add_executable(midiobserve tests/midiobserve.cpp)
  target_link_libraries(midiobserve PRIVATE libremidi)

  add_executable(cmidiin tests/cmidiin.cpp)
  target_link_libraries(cmidiin PRIVATE libremidi)

  add_executable(midiclock_in tests/midiclock_in.cpp)
  target_link_libraries(midiclock_in PRIVATE libremidi)

  add_executable(midiclock_out tests/midiclock_out.cpp)
  target_link_libraries(midiclock_out PRIVATE libremidi)

  add_executable(midiout tests/midiout.cpp)
  target_link_libraries(midiout PRIVATE libremidi)

  if(MSVC)
    set(CMAKE_REQUIRED_FLAGS /std:c++20)
  else()
    set(CMAKE_REQUIRED_FLAGS -std=c++2a)
  endif()
  check_cxx_source_compiles("#include <thread>\nint main() { std::jthread t; }" HAS_STD_JTHREAD)

  if(HAS_STD_JTHREAD)
    add_executable(multithread_midiout tests/multithread_midiout.cpp)
    target_compile_features(multithread_midiout PRIVATE cxx_std_20)
    target_link_libraries(multithread_midiout PRIVATE libremidi)
  endif()

  add_executable(midiprobe tests/midiprobe.cpp)
  target_link_libraries(midiprobe PRIVATE libremidi)

  add_executable(qmidiin tests/qmidiin.cpp)
  target_link_libraries(qmidiin PRIVATE libremidi)

  add_executable(sysextest tests/sysextest.cpp)
  target_link_libraries(sysextest PRIVATE libremidi)

  add_executable(midi2 tests/midi2.cpp)
  target_link_libraries(midi2 PRIVATE libremidi)

  if(EMSCRIPTEN)
    add_executable(emscripten_midiin tests/emscripten_midiin.cpp)
    target_link_libraries(emscripten_midiin PRIVATE libremidi)
  endif()
endif()

if(LIBREMIDI_TESTS)
  find_package(Catch2 QUIET)
  if(TARGET Catch2::Catch2WithMain AND Catch2_FOUND)
    message(STATUS "libremidi: compiling tests")
    target_compile_features(libremidi ${_public} cxx_std_20)

    add_executable(midiin_test tests/unit/midi_in.cpp)
    target_link_libraries(midiin_test PRIVATE libremidi Catch2::Catch2WithMain)

    add_executable(midiout_test tests/unit/midi_out.cpp)
    target_link_libraries(midiout_test PRIVATE libremidi Catch2::Catch2WithMain)

    add_executable(midifile_read_test tests/unit/midifile_read.cpp)
    target_link_libraries(midifile_read_test PRIVATE libremidi Catch2::Catch2WithMain)
    target_compile_definitions(midifile_read_test PRIVATE "LIBREMIDI_TEST_CORPUS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus\"")

    add_executable(midifile_write_test tests/unit/midifile_write.cpp)
    target_link_libraries(midifile_write_test PRIVATE libremidi Catch2::Catch2WithMain)
    target_compile_definitions(midifile_write_test PRIVATE "LIBREMIDI_TEST_CORPUS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus\"")

    add_executable(midifile_write_tracks_test tests/integration/midifile_write_tracks.cpp)
    target_link_libraries(midifile_write_tracks_test PRIVATE libremidi Catch2::Catch2WithMain)

    include(CTest)
    include(Catch)
    catch_discover_tests(midiin_test)
    catch_discover_tests(midiout_test)
    catch_discover_tests(midifile_read_test)
    catch_discover_tests(midifile_write_test)
    catch_discover_tests(midifile_write_tracks_test)
  else()
    message(STATUS "libremidi: not compiling tests")
  endif()
endif()
