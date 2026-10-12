if(LIBREMIDI_NO_JACK)
  return()
endif()

if(NOT LIBREMIDI_HAS_STD_SEMAPHORE)
  message(STATUS "libremidi: std::binary_semaphore not available, skipping JACK backend")
  set(LIBREMIDI_HAS_JACK 0)
  return()
endif()

find_path(JACK_PATH jack/jack.h)

if(JACK_PATH)
  message(STATUS "libremidi: using JACK (dynamic loading)")
  set(LIBREMIDI_HAS_JACK 1)

  target_include_directories(libremidi SYSTEM ${_public} $<BUILD_INTERFACE:${JACK_PATH}>)
  if(WIN32)
    # LoadLibrary/GetProcAddress are always available on Windows
  else()
    target_link_libraries(libremidi ${_public} ${CMAKE_DL_LIBS})
  endif()
endif()

if(LIBREMIDI_HAS_JACK)
  target_compile_definitions(libremidi ${_public} LIBREMIDI_JACK)
  # Check if jack_get_version is available (JACK2+, needed for UMP support detection)
  # block() needs CMake 3.25; the check state push and pop scope the same
  # variable on the minimum this project declares.
  cmake_push_check_state()
    set(CMAKE_REQUIRED_INCLUDES $<BUILD_INTERFACE:${JACK_PATH}>)
    check_cxx_source_compiles("#include <jack/jack.h>\nint main() { (void)&jack_get_version; return 0; }" LIBREMIDI_HAS_JACK_GET_VERSION)
    if(LIBREMIDI_HAS_JACK_GET_VERSION)
      target_compile_definitions(libremidi ${_public} LIBREMIDI_HAS_JACK_GET_VERSION)
    endif()
  cmake_pop_check_state()
endif()
