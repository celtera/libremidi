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

// Fills ports with every device whose capabilities could be read. An unreadable
// device takes no occurrence number, here and in find_port. Returns whether
// every device was read. cancelled() is checked before each capability read,
// which a driver can make slow; a cancelled listing is incomplete too.
template <typename Port>
bool get_port_list(std::vector<Port>& ports, const auto& cancelled)
{
  const UINT count = device_count<Port>();
  ports.reserve(count); // The name views below point into these ports.
  std::unordered_map<std::string_view, UINT> occurrences;
  bool complete = true;
  for (UINT i = 0; i < count; ++i)
  {
    if (cancelled())
      return false;
    port_caps caps;
    if (!read_caps<Port>(i, caps))
    {
      complete = false;
      continue;
    }
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
  return complete;
}

// "<raw name> <occurrence>", as get_port_list writes it.
inline bool
parse_numbered_name(std::string_view display, std::string_view& raw, UINT& ordinal) noexcept
{
  const auto space = display.rfind(' ');
  if (space == display.npos)
    return false;
  const auto suffix = display.substr(space + 1);
  if (suffix.empty() || suffix.front() == '0')
    return false;
  const auto end = suffix.data() + suffix.size();
  const auto [ptr, ec] = std::from_chars(suffix.data(), end, ordinal);
  if (ec != std::errc{} || ptr != end)
    return false;
  raw = display.substr(0, space);
  return true;
}

template <typename Port>
bool get_port_list(std::vector<Port>& ports)
{
  return get_port_list(ports, [] { return false; });
}

template <typename Port>
std::optional<UINT> find_port(const Port& wanted) noexcept
{
  const std::string_view port_name = wanted.port_name, display = wanted.display_name;

  // The old index is not an identity. A listed port names an occurrence.
  std::string_view numbered;
  UINT ordinal = 0;
  if (!parse_numbered_name(display, numbered, ordinal)
      || (!port_name.empty() && numbered != port_name))
    ordinal = 0;

  // Other selectors, and occurrences that are gone, open by raw name: the
  // first device of that name, as before occurrences were numbered.
  const std::string_view raw = port_name.empty() ? display : port_name;
  if (ordinal == 0 && raw.empty())
    return {};

  // Among devices of the same name, one of the wanted model is preferred.
  // Another model is still opened: these identifiers are often generic.
  const auto* device = libremidi::get_if<usb_device_identifier>(&wanted.device);
  std::optional<UINT> occurrence, numbered_model, numbered_first, raw_model, raw_first;
  UINT seen = 0;
  const UINT count = device_count<Port>();
  for (UINT i = 0; i < count; ++i)
  {
    port_caps caps;
    if (!read_caps<Port>(i, caps))
      continue;
    const bool model = device && *device == caps.device;
    const auto name = caps.name();
    if (ordinal != 0 && name == numbered)
    {
      if (!numbered_first)
        numbered_first = i;
      if (model && !numbered_model)
        numbered_model = i;
      if (++seen == ordinal)
      {
        if (!device || model)
          return i;
        occurrence = i;
      }
      if (occurrence && numbered_model)
        return numbered_model;
    }
    if (name == raw)
    {
      if (ordinal == 0 && (!device || model))
        return i;
      if (!raw_first)
        raw_first = i;
      if (model && !raw_model)
        raw_model = i;
    }
  }
  for (auto match : {occurrence, raw_model, raw_first, numbered_model, numbered_first})
    if (match)
      return match;
  return {};
}
}
