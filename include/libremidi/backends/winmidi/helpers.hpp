#pragma once
// clang-format off
#if !defined(NOMINMAX)
  #define NOMINMAX 1
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
  #define WIN32_LEAN_AND_MEAN 1
#endif
#include <libremidi/detail/midi_api.hpp>

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

// Do not use version helpers: the host application's manifest is outside our control.
inline bool service_available() noexcept
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

  // MidiApi is a static runtimeclass. Resolve its factory, not an instance;
  // a supported build may still lack the API or be in Legacy API mode.
  const auto api = try_get_activation_factory<MidiApi, IMidiApiStatics>();
  return api && api.EnsureServiceAvailable();
#else
  return false;
#endif
}

inline midi2v::MidiVirtualDeviceCreationConfig setup_virtualdevice_config(
    std::string_view manufacturer_name,
    std::string_view product_id,
    std::string_view port_name,
    MidiFunctionBlockDirection direction)
{
  using namespace midi2v;

  MidiDeclaredEndpointInfo endpointInfo;
  endpointInfo.HasStaticFunctionBlocks(true);
  endpointInfo.DeclaredFunctionBlockCount(1);
  endpointInfo.Name(to_hstring(port_name));
  endpointInfo.ProductInstanceId(to_hstring(product_id));
  endpointInfo.SupportsMidi10Protocol(true);
  endpointInfo.SupportsMidi20Protocol(true);
  endpointInfo.SupportsReceivingJitterReductionTimestamps(false);
  endpointInfo.SupportsSendingJitterReductionTimestamps(false);
  endpointInfo.SpecificationVersionMajor(1);
  endpointInfo.SpecificationVersionMinor(1);

  // Create the virtual device configuration
  if(manufacturer_name.empty())
    manufacturer_name = "libremidi";

  MidiVirtualDeviceCreationConfig creationConfig(
      to_hstring(port_name), // name
      to_hstring(std::string("Virtual input port: ") + std::string(port_name)), // description
      to_hstring(manufacturer_name), // manufacturer
      endpointInfo);

  // Add a default function block for the virtual device
  MidiFunctionBlock block0;
  block0.Number(0);
  block0.IsActive(true);
  block0.Name(to_hstring(port_name));
  block0.FirstGroup(MidiGroup(static_cast<uint8_t>(0)));
  block0.GroupCount(16);  // All 16 groups
  block0.Direction(direction);
  creationConfig.FunctionBlocks().Append(block0);

  return creationConfig;

}
}
