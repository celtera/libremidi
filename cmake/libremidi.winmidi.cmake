if(LIBREMIDI_NO_WINMIDI)
  return()
endif()

file(DOWNLOAD
  https://github.com/microsoft/MIDI/releases/download/dev-preview-5/all-headers-sdk-10.0.22621.0-plus-dp5.zip
  "${CMAKE_BINARY_DIR}/cppwinrt-headers.zip"
)
file(ARCHIVE_EXTRACT
  INPUT "${CMAKE_BINARY_DIR}/cppwinrt-headers.zip"
  DESTINATION "${CMAKE_BINARY_DIR}/cppwinrt/winrt/"
)

target_include_directories(libremidi SYSTEM ${_public}
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/cppwinrt>
)
target_compile_definitions(libremidi ${_public} LIBREMIDI_WINMIDI)
set(LIBREMIDI_HAS_WINMIDI 1)
target_link_libraries(libremidi INTERFACE RuntimeObject)
