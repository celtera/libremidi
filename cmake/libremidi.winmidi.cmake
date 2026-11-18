if(LIBREMIDI_NO_WINMIDI OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  return()
endif()
if(NOT LIBREMIDI_WINMIDI_HEADERS_ZIP AND NOT LIBREMIDI_DOWNLOAD_CPPWINRT)
  return()
endif()

# Match the generator to the base projections; different C++/WinRT releases are incompatible.
get_filename_component(_projection_prefix "${CPPWINRT_PATH}" DIRECTORY)
if(CPPWINRT_PATH STREQUAL _kit_projection)
  set(_tool_dirs "${CMAKE_WINDOWS_KITS_10_DIR}/bin/${_kit_version}/x86")
else()
  set(_tool_dirs "${_projection_prefix}/bin")
endif()
find_program(CPPWINRT_TOOL cppwinrt HINTS ${_tool_dirs})
find_file(WINMIDI_PLATFORM_WINMD Windows.winmd
  HINTS "${CMAKE_WINDOWS_KITS_10_DIR}/UnionMetadata/${_kit_version}")
if(NOT CPPWINRT_PATH OR NOT CPPWINRT_TOOL OR NOT WINMIDI_PLATFORM_WINMD)
  message(STATUS "libremidi: WinMIDI needs C++/WinRT headers, matching cppwinrt and Windows SDK metadata")
  return()
endif()

set(_sdk "${CMAKE_CURRENT_BINARY_DIR}/winmidi-sdk")
set(_projection "${_sdk}/projection")
set(_package "${LIBREMIDI_WINMIDI_HEADERS_ZIP}")
if(NOT _package)
  set(_version 0.99.92-preview.11)
  set(_package "${_sdk}/Windows.Devices.Midi2.${_version}.nupkg")
  file(DOWNLOAD
    "https://github.com/microsoft/MIDI/releases/download/inbox-preview-11/Windows.Devices.Midi2.${_version}.nupkg"
    "${_package}" TLS_VERIFY ON
    EXPECTED_HASH SHA256=13afc6e6da2adac309ab8c174352f46f1c0ebe2d79e65475bd6274defb73df74)
endif()
# Only metadata and native COM declarations. Never extract/deploy a runtime DLL.
file(ARCHIVE_EXTRACT INPUT "${_package}" DESTINATION "${_sdk}"
  PATTERNS ref/native/Windows.Devices.Midi2.winmd build/native/include/WindowsMidiServicesAppSdkComExtensions*)
execute_process(COMMAND "${CPPWINRT_TOOL}"
  -input "${_sdk}/ref/native/Windows.Devices.Midi2.winmd"
  -reference "${WINMIDI_PLATFORM_WINMD}" -output "${_projection}"
  COMMAND_ERROR_IS_FATAL ANY)
# Use the toolchain's winrt/base.h (MinGW packages include platform adaptations).
file(GLOB _generated_base "${_projection}/winrt/base*.h")
if(_generated_base)
  file(REMOVE ${_generated_base})
endif()
file(COPY "${_sdk}/build/native/include/" DESTINATION "${_projection}")

message(STATUS "libremidi: using Windows MIDI Services (Windows.Devices.Midi2)")
set(LIBREMIDI_HAS_WINMIDI 1)
target_include_directories(libremidi SYSTEM ${_public}
  $<BUILD_INTERFACE:${_projection}> $<BUILD_INTERFACE:${CPPWINRT_PATH}>
  $<INSTALL_INTERFACE:include/libremidi-winmidi>)
install(DIRECTORY "${_projection}/" DESTINATION include/libremidi-winmidi)
target_compile_definitions(libremidi ${_public} LIBREMIDI_WINMIDI)
target_link_libraries(libremidi ${_public} runtimeobject windowsapp)
