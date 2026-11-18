#pragma once
#include <libremidi/backends/winmm/error_domain.hpp>
#include <libremidi/port_information.hpp>

#include <charconv>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

NAMESPACE_LIBREMIDI::winmm
{

template <typename Port>
UINT device_count() noexcept
{
  if constexpr (std::is_same_v<Port, input_port>)
    return midiInGetNumDevs();
  else
    return midiOutGetNumDevs();
}

struct port_caps
{
  // One UTF-16 code unit needs at most three UTF-8 bytes (a pair needs four).
  char utf8[MAXPNAMELEN * 3];
  int length;
  usb_device_identifier device;
  std::string_view name() const noexcept { return {utf8, static_cast<std::size_t>(length)}; }
};

template <typename Port>
bool read_caps(UINT index, port_caps& port) noexcept
{
  constexpr bool input = std::is_same_v<Port, input_port>;
  std::conditional_t<input, MIDIINCAPSW, MIDIOUTCAPSW> caps{};
  MMRESULT result;
  if constexpr (input)
    result = midiInGetDevCapsW(index, &caps, sizeof(caps));
  else
    result = midiOutGetDevCapsW(index, &caps, sizeof(caps));
  if (result != MMSYSERR_NOERROR)
    return false;

  const int length = WideCharToMultiByte(
      CP_UTF8, 0, caps.szPname, -1, port.utf8, sizeof(port.utf8), nullptr, nullptr);
  if (length == 0)
    return false;
  port.length = length - 1;
  port.device = {caps.wMid, caps.wPid};
  return true;
}

template <typename Port>
bool get_port_list(std::vector<Port>& ports)
{
  const UINT count = device_count<Port>();
  ports.reserve(count); // The name views below point into these ports.
  std::unordered_map<std::string_view, UINT> occurrences;
  for (UINT i = 0; i < count; ++i)
  {
    port_caps caps;
    if (!read_caps<Port>(i, caps))
      return false; // Later occurrences cannot be numbered reliably past a gap.
    auto& port = ports.emplace_back();
    port.api = libremidi::API::WINDOWS_MM;
    port.client = 0;
    port.port = i;
    port.device = caps.device;
    port.port_name = caps.name();
    const auto ordinal = std::to_string(++occurrences[port.port_name]);
    port.display_name.reserve(port.port_name.size() + 1 + ordinal.size());
    port.display_name.append(port.port_name).append(" ").append(ordinal);
  }
  return true;
}

template <typename Port>
std::optional<UINT> find_port(const Port& wanted) noexcept
{
  std::string_view name = wanted.port_name;
  UINT ordinal = 1;
  const bool numbered = !wanted.display_name.empty();
  if (numbered)
  {
    const std::string_view display = wanted.display_name;
    const auto space = display.rfind(' ');
    if (space == display.npos)
      return {};
    const auto suffix = display.substr(space + 1);
    if (suffix.empty() || suffix.front() == '0')
      return {};
    const auto end = suffix.data() + suffix.size();
    const auto [ptr, ec] = std::from_chars(suffix.data(), end, ordinal);
    if (ec != std::errc{} || ptr != end)
      return {};
    const auto raw = display.substr(0, space);
    if (!name.empty() && raw != name)
      return {};
    name = raw;
  }
  else if (name.empty())
    return {};

  const auto* device = libremidi::get_if<usb_device_identifier>(&wanted.device);
  std::optional<UINT> raw_match;
  const UINT count = device_count<Port>();
  for (UINT i = 0; i < count; ++i)
  {
    // The old index is not an identity: even a matching name and model can
    // be a different occurrence after reordering. Numbered names only need
    // a prefix scan. Raw-only names must be unambiguous across the full list.
    port_caps caps;
    if (!read_caps<Port>(i, caps))
      return {}; // An unread name makes the occurrence number uncertain.
    if (caps.name() != name)
      continue;
    if (numbered)
    {
      if (--ordinal != 0)
        continue;
      if (device && *device != caps.device)
        return {}; // Never substitute another occurrence.
      return i;
    }
    if (!device || *device == caps.device)
    {
      if (raw_match)
        return {};
      raw_match = i;
    }
  }
  return raw_match;
}
}
