if(LIBREMIDI_NO_WINUWP)
  return()
endif()

if(LIBREMIDI_HAS_WINMIDI)
  set(LIBREMIDI_HAS_WINUWP 1)
  message(STATUS "libremidi: using WinUWP")

  target_compile_options(libremidi ${_public} /EHsc)
  target_compile_definitions(libremidi ${_public} LIBREMIDI_WINUWP)
  return()
endif()

set(WINSDK_PATH "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]")
# List all the SDKs manually as CMAKE_VS_blabla is only defined for VS generators
cmake_host_system_information(
    RESULT WINSDK_PATH
    QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
          VALUE KitsRoot10)

file(GLOB WINSDK_GLOB RELATIVE "${WINSDK_PATH}Include/" "${WINSDK_PATH}Include/*")
set(WINSDK_LIST)
foreach(dir ${WINSDK_GLOB})
  list(APPEND WINSDK_LIST "Include/${dir}/cppwinrt")
  message(" - WinSDK version: Include/${dir}/cppwinrt")
endforeach()

find_path(CPPWINRT_PATH "winrt/base.h"
  PATHS
    "${WINSDK_PATH}"
  PATH_SUFFIXES
    "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
    "Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
    ${WINSDK_LIST}
)
message("winrt paths:\n -- ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt\n -- Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt\n -- ${WINSDK_LIST}")
message("final path:\n -- ${CPPWINRT_PATH}")

if(CPPWINRT_PATH)
  message(STATUS "libremidi: using WinUWP")
  set(LIBREMIDI_HAS_WINUWP 1)

  target_include_directories(libremidi SYSTEM ${_public} "${CPPWINRT_PATH}")
  target_compile_definitions(libremidi ${_public} LIBREMIDI_WINUWP)
  target_link_libraries(libremidi INTERFACE RuntimeObject)
  # We don't need /ZW option here (support for C++/CX)' as we use C++/WinRT
  target_compile_options(libremidi ${_public} /EHsc)
else()
  message(STATUS "libremidi: Failed to find Windows SDK, UWP MIDI backend will not be available")
  return()
endif()

