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
#include <cctype>
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

inline bool ichar_equals(char a, char b)
{
  return std::tolower(static_cast<unsigned char>(a))
         == std::tolower(static_cast<unsigned char>(b));
}

inline bool iequals(std::string_view lhs, std::string_view rhs)
{
  return std::ranges::equal(lhs, rhs, ichar_equals);
}

inline std::pair<MidiEndpointDeviceInformation, MidiGroupTerminalBlock>
get_port(const std::string& device_name, int group_terminal_block)
{
  auto eps = MidiEndpointDeviceInformation::FindAll();
  for (const auto& ep : eps)
  {
    auto str = to_string(ep.EndpointDeviceId());
    if (str.empty())
      continue;

    if (iequals(str, device_name))
    {
      for (const auto& gp : ep.GetGroupTerminalBlocks())
      {
        if (gp.Number() == group_terminal_block)
        {
          return std::make_pair(ep, gp);
        }
      }
    }
  }
  return {nullptr, nullptr};
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
