macro(setup_example _example)
  target_link_libraries(${_example} PRIVATE libremidi)
endmacro()

macro(add_example _example)
  add_executable(${_example} examples/${_example}.cpp)
  setup_example(${_example})
endmacro()

macro(add_backend_example _example)
    add_executable(${_example} examples/backends/${_example}.cpp)
    setup_example(${_example})
endmacro()

add_example(midiobserve)
add_example(echo)
add_example(cmidiin)
add_example(midiclock_in)
add_example(midiclock_out)
add_example(midiout)
add_example(client)
add_example(midiprobe)
add_example(qmidiin)
add_example(sysextest)
add_example(midi2_echo)

if(LIBREMIDI_NI_MIDI2)
  add_example(midi2_interop)
endif()

if(LIBREMIDI_HAS_STD_JTHREAD)
  add_example(multithread_midiout)
endif()

if(LIBREMIDI_HAS_ALSA)
  add_example(poll_share)
  target_link_libraries(poll_share PRIVATE ${ALSA_LIBRARIES})

  add_example(alsa_share)
  target_link_libraries(alsa_share PRIVATE ${ALSA_LIBRARIES})

  add_backend_example(midi1_in_alsa_seq)
  add_backend_example(midi1_out_alsa_seq)

  if(LIBREMIDI_HAS_ALSA_RAWMIDI)
    add_backend_example(midi1_in_alsa_rawmidi)
    add_backend_example(midi1_out_alsa_rawmidi)
  endif()

  if(LIBREMIDI_HAS_ALSA_UMP)
    add_backend_example(midi2_in_alsa_rawmidi)
    add_backend_example(midi2_in_alsa_seq)
    add_backend_example(midi2_out_alsa_rawmidi)
    add_backend_example(midi2_out_alsa_seq)
  endif()
endif()

if(LIBREMIDI_HAS_JACK)
    add_example(jack_share)
endif()

if(LIBREMIDI_HAS_PIPEWIRE)
    add_example(pipewire_share)
    add_backend_example(midi1_in_pipewire)
    add_backend_example(midi1_out_pipewire)
endif()

if(LIBREMIDI_HAS_COREMIDI)
    add_example(coremidi_share)
endif()

if(LIBREMIDI_HAS_EMSCRIPTEN)
    add_example(emscripten_midiin)
endif()

if(LIBREMIDI_HAS_WINMIDI)
  add_backend_example(midi2_in_winmidi)
endif()
