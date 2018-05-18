cmake_minimum_required(VERSION 3.9 FATAL_ERROR)
project(RtMidi17 CXX)

option(RTMIDI17_NO_COREMIDI "Disable CoreMidi back-end" OFF)
option(RTMIDI17_NO_WINMM "Disable WinMM back-end" OFF)
option(RTMIDI17_NO_WINUWP "Disable UWP back-end" OFF)
option(RTMIDI17_NO_JACK "Disable JACK back-end" OFF)
option(RTMIDI17_NO_ALSA "Disable ALSA back-end" OFF)

### Main library ###
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_library(RtMidi17
  rtmidi17/rtmidi17.cpp
  rtmidi17/reader.cpp
  rtmidi17/reader.hpp
  rtmidi17/writer.cpp
  rtmidi17/writer.hpp
  rtmidi17/message.hpp
  rtmidi17/rtmidi17.hpp
  rtmidi17/detail/midi_api.hpp
)

target_compile_features(RtMidi17 PUBLIC cxx_std_17)

find_package(Threads)
target_link_libraries(RtMidi17 PUBLIC ${CMAKE_THREAD_LIBS_INIT})

if(APPLE)
  ## CoreMIDI support ##
  if(NOT RTMIDI17_NO_COREMIDI)
    target_compile_definitions(RtMidi17 PUBLIC RTMIDI17_COREAUDIO)
    target_sources(RtMidi17 PRIVATE rtmidi17/detail/coreaudio.hpp)

    find_library(COREMIDI_LIBRARY CoreMIDI)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)

    target_link_libraries(RtMidi17
      PUBLIC
        ${COREFOUNDATION_LIBRARY}
        ${COREAUDIO_LIBRARY}
        ${COREMIDI_LIBRARY}
     )
  endif()

elseif(WIN32)
  ## WinMM support ##
  if(NOT RTMIDI17_NO_WINMM)
    target_compile_definitions(RtMidi17
      PUBLIC
        RTMIDI17_WINMM
        WIN32=1
        UNICODE=1
        _UNICODE=1
    )
    target_sources(RtMidi17 PRIVATE rtmidi17/detail/winmm.hpp)
    target_link_libraries(RtMidi17 PUBLIC WinMM)
  endif()

  ## UWP MIDI support ##
  if(NOT RTMIDI17_NO_WINUWP)
    set(WINSDK_PATH "[HKLM\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]")
    find_path(CPPWINRT_PATH winrt/base.h HINTS ${WINSDK_PATH}/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt)
    if(CPPWINRT_PATH)
      target_include_directories(RtMidi17 PUBLIC "${CPPWINRT_PATH}")
      target_compile_definitions(RtMidi17 PUBLIC RTMIDI17_WINUWP)
      target_compile_options(RtMidi17 PUBLIC /ZW /EHsc /await)
      target_sources(RtMidi17 PRIVATE rtmidi17/detail/winuwp.hpp)
    endif()
  endif()

elseif(UNIX)
  ## ALSA support ##
  if(NOT RTMIDI17_NO_ALSA)
    find_package(ALSA)
    if(ALSA_FOUND)
      target_compile_definitions(RtMidi17 PUBLIC RTMIDI17_ALSA)
      target_link_libraries(RtMidi17 PUBLIC ${ALSA_LIBRARIES})
      target_sources(RtMidi17 PRIVATE rtmidi17/detail/alsa.hpp)
    endif()
  endif()
endif()

## JACK support ##
if(NOT RTMIDI17_NO_JACK)

  find_library(JACK_LIBRARIES jack)
  if(JACK_LIBRARIES)
    set(HAS_JACK 1)
    target_link_libraries(RtMidi17 PUBLIC ${JACK_LIBRARIES})
  else()
    find_path(WEAKJACK_PATH weakjack/weak_libjack.h)
    if(WEAKJACK_PATH)
      set(HAS_JACK 1)
      target_include_directories(RtMidi17 PUBLIC ${WEAKJACK_PATH})
    endif()
  endif()

  if(HAS_JACK)
    target_compile_definitions(RtMidi17 PUBLIC RTMIDI17_JACK)
    target_sources(RtMidi17
      PRIVATE
        rtmidi17/detail/jack.hpp
        rtmidi17/detail/semaphore.hpp
    )
  endif()
endif()

### Install  ###
install(TARGETS RtMidi17
        EXPORT rtmidi17-exports
        ARCHIVE DESTINATION lib/static
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        )
install(EXPORT rtmidi17-exports
        DESTINATION lib/cmake/rtmidi17)
export(EXPORT rtmidi17-exports)

### Examples ###
add_executable(midiobserve tests/midiobserve.cpp)
target_link_libraries(midiobserve PRIVATE RtMidi17)

add_executable(cmidiin tests/cmidiin.cpp)
target_link_libraries(cmidiin PRIVATE RtMidi17)

add_executable(midiclock_in tests/midiclock_in.cpp)
target_link_libraries(midiclock_in PRIVATE RtMidi17)

add_executable(midiclock_out tests/midiclock_out.cpp)
target_link_libraries(midiclock_out PRIVATE RtMidi17)

add_executable(midiout tests/midiout.cpp)
target_link_libraries(midiout PRIVATE RtMidi17)

add_executable(midiprobe tests/midiprobe.cpp)
target_link_libraries(midiprobe PRIVATE RtMidi17)

add_executable(qmidiin tests/qmidiin.cpp)
target_link_libraries(qmidiin PRIVATE RtMidi17)

add_executable(sysextest tests/sysextest.cpp)
target_link_libraries(sysextest PRIVATE RtMidi17)

