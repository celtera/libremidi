cmake_minimum_required(VERSION 3.9 FATAL_ERROR)
project(RtMidi17 CXX)

### Main library ###
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_library(RtMidi17
  rtmidi17/rtmidi17.cpp
  rtmidi17/rtmidi17.hpp
  rtmidi17/detail/midi_api.hpp
)

target_compile_features(RtMidi17 PUBLIC cxx_std_17)

find_package(Threads)
target_link_libraries(RtMidi17 PUBLIC ${CMAKE_THREAD_LIBS_INIT})

if(APPLE)
  target_compile_definitions(RtMidi17 PUBLIC RTMIDI_COREAUDIO)
  target_sources(RtMidi17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi17/detail/coreaudio.hpp)

  find_library(COREMIDI_LIBRARY CoreMIDI)
  find_library(COREAUDIO_LIBRARY CoreAudio)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)

  target_link_libraries(RtMidi17 PUBLIC
      ${COREFOUNDATION_LIBRARY}
      ${COREAUDIO_LIBRARY}
      ${COREMIDI_LIBRARY})

elseif(WIN32)
  target_compile_definitions(RtMidi17 PUBLIC RTMIDI_WINMM WIN32=1 UNICODE=1 _UNICODE=1)
  target_sources(RtMidi17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi17/detail/winmm.hpp)
  target_link_libraries(RtMidi17 PUBLIC WinMM)

  target_compile_definitions(RtMidi17 PUBLIC RTMIDI_WINUWP)
  target_compile_options(RtMidi17 PUBLIC /ZW /EHsc /await)
  target_sources(RtMidi17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi17/detail/winuwp.hpp)
  target_include_directories(RtMidi17 PUBLIC "C:/Program Files (x86)/Windows Kits/10/Include/10.0.17134.0/cppwinrt")
  # target_link_libraries(RtMidi17 PUBLIC RuntimeObject.lib)
elseif(UNIX)
  find_package(ALSA)
  if(ALSA_FOUND)
    target_compile_definitions(RtMidi17 PUBLIC RTMIDI_ALSA)
    target_link_libraries(RtMidi17 PUBLIC ${ALSA_LIBRARIES})
    target_sources(RtMidi17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi17/detail/alsa.hpp)
  endif()
endif()

find_library(JACK_LIBRARIES jack)
if(JACK_LIBRARIES)
  target_link_libraries(RtMidi17 PUBLIC ${JACK_LIBRARIES})
  target_compile_definitions(RtMidi17 PUBLIC RTMIDI_JACK)
  target_sources(RtMidi17
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi17/detail/jack.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi17/detail/semaphore.hpp
  )
endif()

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
