if(LIBREMIDI_NO_WINMIDI)
  return()
endif()

if(NOT EXISTS "${CMAKE_BINARY_DIR}/winmidi-headers.zip")
  file(DOWNLOAD
    https://github.com/microsoft/MIDI/releases/download/dev-preview-9-namm-4/Microsoft.Windows.Devices.Midi2.1.0.2-preview-9.250121-1820.nupkg
    "${CMAKE_BINARY_DIR}/winmidi-headers.zip"
  )
endif()
file(ARCHIVE_EXTRACT
  INPUT "${CMAKE_BINARY_DIR}/winmidi-headers.zip"
  DESTINATION "${CMAKE_BINARY_DIR}/winmidi-headers/"
)
file(MAKE_DIRECTORY
    "${CMAKE_BINARY_DIR}/cppwinrt/"
)
file(
    COPY
      "${CMAKE_BINARY_DIR}/winmidi-headers/build/native/include/winmidi"
    DESTINATION
      "${CMAKE_BINARY_DIR}/cppwinrt/"
)

target_include_directories(libremidi SYSTEM ${_public}
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/cppwinrt>
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/cppwinrt/winmidi>
)
target_compile_definitions(libremidi ${_public} LIBREMIDI_WINMIDI)
set(LIBREMIDI_HAS_WINMIDI 1)
target_link_libraries(libremidi INTERFACE RuntimeObject)
