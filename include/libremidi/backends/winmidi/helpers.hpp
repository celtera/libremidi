#pragma once
// clang-format off
#if !defined(NOMINMAX)
  #define NOMINMAX 1
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
  #define WIN32_LEAN_AND_MEAN 1
#endif
#include <libremidi/detail/midi_api.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <libremidi/backends/winmidi/group_ports.hpp>
#include <string>
#include <guiddef.h>
#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.h>
#include <winrt/Windows.Devices.Midi2.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.Transports.Virtual.h>
#include <libremidi/backends/winmidi/com.hpp>
#include <libremidi/cmidi2.hpp>

#define LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE 1
// clang-format on

namespace foundation = winrt::Windows::Foundation;

NAMESPACE_LIBREMIDI::winmidi
{
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Devices::Midi2;
using namespace winrt::Windows::Devices::Midi2::Enumeration;
namespace midi2v = winrt::Windows::Devices::Midi2::Transports::Virtual;

inline MidiEndpointDeviceInformation get_port(const port_information& port, port_direction dir)
{
  return resolve_group(port, dir, [](const std::string& id) {
    return MidiEndpointDeviceInformation::CreateFromEndpointDeviceId(to_hstring(id));
  });
}

// Whether this system has the Windows MIDI Services runtime. Cheap, and never
// starts the service: safe for listing and selecting backends.
// Do not use version helpers: the host application's manifest is outside our control.
inline bool runtime_available() noexcept
{
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
  static const bool supported_os = [] {
    using get_version = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto version = std::bit_cast<get_version>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    return version && version(&os) == 0 && os.dwBuildNumber >= 26200;
  }();
  if (!supported_os)
    return false;

  // Only a positive answer is kept: the runtime can be deployed while we run.
  static std::atomic_bool found{false};
  if (found.load(std::memory_order_relaxed))
    return true;

  // MidiApi is a static runtimeclass. Resolve its factory, not an instance;
  // a supported build may still lack the API.
  if (!try_get_activation_factory<MidiApi, IMidiApiStatics>())
    return false;
  found.store(true, std::memory_order_relaxed);
  return true;
#else
  return false;
#endif
}

// Starts the service when it is not running, which can take seconds: only
// call it when an object is about to be created. False in Legacy API mode.
inline bool service_available() noexcept
{
  if (!runtime_available())
    return false;
  const auto api = try_get_activation_factory<MidiApi, IMidiApiStatics>();
  return api && api.EnsureServiceAvailable();
}

}
