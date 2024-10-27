if(NOT WIN32)
  return()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES WindowsStore)
  set(LIBREMIDI_NO_WINMM 1)
endif()

if(NOT LIBREMIDI_NO_WINMM)
  include(libremidi.winmm)
endif()

if(NOT LIBREMIDI_NO_WINMIDI)
  include(libremidi.winmidi)
endif()

if(NOT LIBREMIDI_NO_WINUWP)
  include(libremidi.winuwp)
endif()
