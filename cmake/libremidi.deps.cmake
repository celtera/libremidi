### C++ features ###
check_cxx_source_compiles("#include <thread>\nint main() { std::jthread t; }" LIBREMIDI_HAS_STD_JTHREAD)
check_cxx_source_compiles("#include <semaphore>\nint main() { std::binary_semaphore t{0}; }" LIBREMIDI_HAS_STD_SEMAPHORE)

### Dependencies ###
find_package(Threads)

# ni-midi2
if(LIBREMIDI_NI_MIDI2 AND NOT TARGET ni::midi2)
  FetchContent_Declare(
      ni-midi2
      GIT_REPOSITORY https://github.com/midi2-dev/ni-midi2
      GIT_TAG        main
  )

  FetchContent_MakeAvailable(ni-midi2)
endif()

# boost
if(LIBREMIDI_NO_BOOST AND LIBREMIDI_FIND_BOOST)
  message(FATAL_ERROR "LIBREMIDI_NO_BOOST and LIBREMIDI_FIND_BOOST are incompatible")
endif()

if(LIBREMIDI_FIND_BOOST)
  find_package(Boost REQUIRED)
endif()

# readerwriterqueue

if(NOT LIBREMIDI_NO_PIPEWIRE AND NOT TARGET readerwriterqueue)
  FetchContent_Declare(
      readerwriterqueue
      GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue
      GIT_TAG        master
  )

  FetchContent_MakeAvailable(readerwriterqueue)
endif()
