#pragma once
// clang-format off
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <libremidi/detail/midi_api.hpp>

#include <mutex>
#include <cctype>
#include <string>
#include <thread>
#include <vector>
#include <guiddef.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.h>


namespace midi2 = winrt::Windows::Devices::Midi2;
namespace foundation = winrt::Windows::Foundation;
namespace collections = winrt::Windows::Foundation::Collections;


// clang-format on

namespace libremidi::winmidi
{
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Devices::Midi2;
using namespace Windows::Devices::Enumeration;

inline bool ichar_equals(char a, char b)
{
  return std::tolower(static_cast<unsigned char>(a)) ==
         std::tolower(static_cast<unsigned char>(b));
}
inline bool iequals(std::string_view lhs, std::string_view rhs)
{
  return std::ranges::equal(lhs, rhs, ichar_equals);
}

inline winrt::Windows::Devices::Midi2::MidiEndpointDeviceInformation
get_port_by_name(const std::string& port) {
  auto eps = MidiEndpointDeviceInformation::FindAll();
  for (const auto& ep : eps)
  {
    auto str = to_string(ep.Id());
    if (str.empty())
      continue;

    if (iequals(str, port))
      return ep;
  }
  return {nullptr};
}
}
