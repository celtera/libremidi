if(APPLE)
  return()
endif()
if(NOT UNIX)
  return()
endif()
if(NOT LIBREMIDI_HAS_STD_SEMAPHORE)
  return()
endif()

find_path(PIPEWIRE_PATH pipewire/pipewire.h HINTS /usr/include/pipewire-0.3)
find_path(PIPEWIRE_SPA_PATH spa/control/control.h HINTS /usr/include/spa-0.2)

if(PIPEWIRE_PATH AND PIPEWIRE_SPA_PATH)
  message(STATUS "libremidi: using PipeWire")
  set(LIBREMIDI_HAS_PIPEWIRE 1)

  target_compile_definitions(libremidi
    ${_public}
      LIBREMIDI_PIPEWIRE
  )
  target_include_directories(libremidi SYSTEM
    ${_public}
      $<BUILD_INTERFACE:${PIPEWIRE_PATH}> $<BUILD_INTERFACE:${PIPEWIRE_SPA_PATH}>
  )
  target_link_libraries(libremidi
    ${_public}
      ${CMAKE_DL_LIBS}
      $<BUILD_INTERFACE:readerwriterqueue>
  )
endif()
