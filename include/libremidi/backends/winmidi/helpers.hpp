#pragma once
// clang-format off
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/detail/memory.hpp>

#include <cctype>
#include <string>
#include <guiddef.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Microsoft.Windows.Devices.Midi2.h>
#include <libremidi/cmidi2.hpp>

// MinGW support
#if !defined(_MSC_VER)
namespace Microsoft::Windows::Devices::Midi2::Initialization
{
struct IMidiClientInitializer;
struct MidiClientInitializerUuid;
}

__CRT_UUID_DECL(Microsoft::Windows::Devices::Midi2::Initialization::IMidiClientInitializer,
 0x8087b303, 0xd551, 0xbce2, 0x1e, 0xad, 0xa2, 0x50, 0x0d, 0x50, 0xc5, 0x80);
__CRT_UUID_DECL(Microsoft::Windows::Devices::Midi2::Initialization::MidiClientInitializerUuid,
 0xc3263827, 0xc3b0, 0xbdbd, 0x25, 0x00, 0xce, 0x63, 0xa3, 0xf3, 0xf2, 0xc3);
#endif

#include <init/Microsoft.Windows.Devices.Midi2.Initialization.hpp>
// clang-format on

namespace midi2 = winrt::Microsoft::Windows::Devices::Midi2;
namespace foundation = winrt::Windows::Foundation;

namespace libremidi::winmidi
{
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Microsoft::Windows::Devices::Midi2;
using namespace Windows::Devices::Enumeration;
using namespace ::Microsoft::Windows::Devices::Midi2::Initialization;

inline bool ichar_equals(char a, char b)
{
  return std::tolower(static_cast<unsigned char>(a)) ==
         std::tolower(static_cast<unsigned char>(b));
}

inline bool iequals(std::string_view lhs, std::string_view rhs)
{
  return std::ranges::equal(lhs, rhs, ichar_equals);
}

inline std::pair<
    winrt::Microsoft::Windows::Devices::Midi2::MidiEndpointDeviceInformation,
    winrt::Microsoft::Windows::Devices::Midi2::MidiGroupTerminalBlock>
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
struct winmidi_shared_data
{
  std::shared_ptr<MidiDesktopAppSdkInitializer> initializer;
  bool m_ready{false};
  winmidi_shared_data()
      : initializer{libremidi::instance<MidiDesktopAppSdkInitializer>()}
      , m_ready{
            initializer && initializer->InitializeSdkRuntime()
            && initializer->EnsureServiceAvailable()}
  {
  }
};
}
