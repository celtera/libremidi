#pragma once

#if __has_include(<WindowsMidiServicesAppSdkComExtensions.h>)
#include <WindowsMidiServicesAppSdkComExtensions.h>
// Use the MIDL-generated selectany definitions, including in header-only builds.
// MinGW's __uuidof cannot infer UUIDs from this SDK's MIDL_INTERFACE declarations.
#include <WindowsMidiServicesAppSdkComExtensions_i.c>
#define LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS 1
#endif
