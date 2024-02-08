
add_executable(midiobserve examples/midiobserve.cpp)
target_link_libraries(midiobserve PRIVATE libremidi)

add_executable(echo examples/echo.cpp)
target_link_libraries(echo PRIVATE libremidi)

add_executable(cmidiin examples/cmidiin.cpp)
target_link_libraries(cmidiin PRIVATE libremidi)

add_executable(midiclock_in examples/midiclock_in.cpp)
target_link_libraries(midiclock_in PRIVATE libremidi)

add_executable(midiclock_out examples/midiclock_out.cpp)
target_link_libraries(midiclock_out PRIVATE libremidi)

add_executable(midiout examples/midiout.cpp)
target_link_libraries(midiout PRIVATE libremidi)

if(HAS_STD_JTHREAD)
    add_executable(multithread_midiout examples/multithread_midiout.cpp)
    target_link_libraries(multithread_midiout PRIVATE libremidi)
endif()

add_executable(midiclient examples/client.cpp)
target_link_libraries(midiclient PRIVATE libremidi)

add_executable(midiprobe examples/midiprobe.cpp)
target_link_libraries(midiprobe PRIVATE libremidi)

add_executable(qmidiin examples/qmidiin.cpp)
target_link_libraries(qmidiin PRIVATE libremidi)

add_executable(sysextest examples/sysextest.cpp)
target_link_libraries(sysextest PRIVATE libremidi)

add_executable(midi2_echo examples/midi2_echo.cpp)
target_link_libraries(midi2_echo PRIVATE libremidi)

if(LIBREMIDI_HAS_ALSA)
    add_executable(poll_share examples/poll_share.cpp)
    target_link_libraries(poll_share PRIVATE libremidi ${ALSA_LIBRARIES})
    add_executable(alsa_share examples/alsa_share.cpp)
    target_link_libraries(alsa_share PRIVATE libremidi ${ALSA_LIBRARIES})
endif()

if(LIBREMIDI_HAS_JACK)
    add_executable(jack_share examples/jack_share.cpp)
    target_link_libraries(jack_share PRIVATE libremidi)
endif()

if(LIBREMIDI_HAS_COREMIDI)
    add_executable(coremidi_share examples/coremidi_share.cpp)
    target_link_libraries(coremidi_share PRIVATE libremidi)
endif()

if(LIBREMIDI_HAS_EMSCRIPTEN)
    add_executable(emscripten_midiin examples/emscripten_midiin.cpp)
    target_link_libraries(emscripten_midiin PRIVATE libremidi)
endif()
