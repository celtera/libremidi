if(NOT UNIX)
  return()
endif()
if(EMSCRIPTEN OR APPLE)
  return()
endif()
if(LIBREMIDI_NO_PIPEWIRE)
  return()
endif()

if(NOT LIBREMIDI_HAS_STD_SEMAPHORE)
  return()
endif()
if(NOT LIBREMIDI_HAS_STD_STOP_TOKEN)
  return()
endif()

find_path(PIPEWIRE_INCLUDEDIR pipewire-0.3/pipewire/filter.h)
find_path(SPA_INCLUDEDIR spa-0.2/spa/param/latency-utils.h)

if(PIPEWIRE_INCLUDEDIR AND SPA_INCLUDEDIR)
  message(STATUS "libremidi: using PipeWire")
  set(LIBREMIDI_HAS_PIPEWIRE 1)

  target_compile_options(libremidi
    ${_public}
      $<$<BOOL:${LIBREMIDI_CXX_HAS_WNO_GNU_STATEMENT}>:-Wno-gnu-statement-expression-from-macro-expansion>
      $<$<BOOL:${LIBREMIDI_CXX_HAS_WNO_C99_EXTENSIONS}>:-Wno-c99-extensions>
  )
  target_compile_definitions(libremidi
    ${_public}
      LIBREMIDI_PIPEWIRE
  )
  target_include_directories(libremidi SYSTEM
    ${_public}
      $<BUILD_INTERFACE:${PIPEWIRE_INCLUDEDIR}/pipewire-0.3>
      $<BUILD_INTERFACE:${SPA_INCLUDEDIR}/spa-0.2>
  )
  target_link_libraries(libremidi
    ${_public}
      ${CMAKE_DL_LIBS}
      $<BUILD_INTERFACE:readerwriterqueue>
  )
endif()
