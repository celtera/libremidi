# Prefer the toolchain's projections (e.g. MSYS2 cppwinrt) or use the Windows Kit.
if(NOT CMAKE_WINDOWS_KITS_10_DIR)
  get_filename_component(CMAKE_WINDOWS_KITS_10_DIR
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
endif()
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
  set(_kit_version "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
else()
  file(GLOB _kits RELATIVE "${CMAKE_WINDOWS_KITS_10_DIR}/UnionMetadata"
    "${CMAKE_WINDOWS_KITS_10_DIR}/UnionMetadata/10.*")
  list(SORT _kits COMPARE NATURAL ORDER DESCENDING)
  if(_kits)
    list(GET _kits 0 _kit_version)
  endif()
endif()
set(_kit_projection "${CMAKE_WINDOWS_KITS_10_DIR}/Include/${_kit_version}/cppwinrt")
find_path(CPPWINRT_PATH winrt/Windows.Devices.Midi.h
  HINTS ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES} ENV INCLUDE
  PATHS "${_kit_projection}")
