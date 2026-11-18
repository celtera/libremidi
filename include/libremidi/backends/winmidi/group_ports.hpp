#pragma once
#include <libremidi/observer_configuration.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

NAMESPACE_LIBREMIDI::winmidi
{
enum class port_direction { input, output };

inline transport_type transport_from_code(std::string_view code) noexcept
{
  using enum transport_type;
  if (code.starts_with("KS")) return hardware;
  if (code.starts_with("BLE")) return transport_type(hardware | bluetooth);
  if (code == "VPB" || code == "APP") return software;
  if (code == "LOOP") return transport_type(software | loopback);
  if (code.starts_with("NET")) return network;
  return unknown;
}

inline bool accepts_endpoint(const libremidi::observer_configuration& conf, transport_type type)
{
  // Preserve hardware tracking for endpoints from unrecognised transports.
  return conf.accepts(type == transport_type::unknown ? transport_type::hardware : type);
}

// The SDK exposes the same range and direction properties on both block types.
// Keep block objects as metadata; only (endpoint, group, direction) is an address.
template <typename Endpoint, typename Emit>
void for_each_group(const Endpoint& ep, Emit emit)
{
  uint16_t inputs{}, outputs{};
  const auto visit = [&](const auto& block) {
    const auto dir = block.Direction();
    using direction = decltype(block.Direction());
    const unsigned first = block.FirstGroup().Index();
    const unsigned end = std::min(16u, first + block.GroupCount());
    for (unsigned group = first; group < end; ++group)
    {
      const auto bit = static_cast<uint16_t>(1u << group);
      // A device's BlockOutput sends to an application input, and vice versa.
      if ((dir == direction::BlockOutput || dir == direction::Bidirectional) && !(inputs & bit))
      {
        inputs |= bit;
        emit(port_direction::input, group, block);
      }
      if ((dir == direction::BlockInput || dir == direction::Bidirectional) && !(outputs & bit))
      {
        outputs |= bit;
        emit(port_direction::output, group, block);
      }
    }
  };

  const auto& fbs = ep.GetDeclaredFunctionBlocks();
  if (fbs.Size() != 0)
  {
    for (const auto& fb : fbs)
      if (fb.IsActive())
        visit(fb);
  }
  else
  {
    for (const auto& gtb : ep.GetGroupTerminalBlocks())
      visit(gtb);
  }
}

inline bool has_group(const auto& ep, port_direction wanted, port_handle address)
{
  bool found = false;
  for_each_group(ep, [&](port_direction dir, unsigned group, const auto&) {
    found |= dir == wanted && address == group;
  });
  return found;
}

inline auto resolve_group(const port_information& port, port_direction dir, auto lookup)
{
  // Older observers stored client = 0 and a block number. It cannot safely
  // be interpreted as a group, even if the number happens to be in [0,15].
  const auto* id = get_if<std::string>(&port.device);
  using endpoint = decltype(lookup(*id));
  if (!id || id->empty() || port.client == 0 || port.port >= 16)
    return endpoint{nullptr};
  auto ep = lookup(*id);
  if (ep && has_group(ep, dir, port.port))
    return ep;
  return endpoint{nullptr};
}

struct group_ports
{
  std::vector<input_port> inputs;
  std::vector<output_port> outputs;
  bool empty() const noexcept { return inputs.empty() && outputs.empty(); }
};

inline group_ports make_group_ports(
    const auto& ep, const port_information& base, bool inputs, bool outputs, auto block_name)
{
  group_ports ports;
  for_each_group(ep, [&](port_direction dir, unsigned group, const auto& block) {
    const bool input = dir == port_direction::input;
    if (input ? !inputs : !outputs) return;
    port_information& p = input
        ? static_cast<port_information&>(ports.inputs.emplace_back())
        : static_cast<port_information&>(ports.outputs.emplace_back());
    p = base;
    p.port = group;
    p.port_name = block_name(block);
    p.display_name = p.device_name + " (Group " + std::to_string(group + 1);
    if (!p.port_name.empty()) p.display_name += ", " + p.port_name;
    p.display_name += ")";
  });
  return ports;
}

struct group_changes
{
  group_ports removed, added;

  void notify(const libremidi::observer_configuration& conf) const
  {
    for (const auto& p : removed.inputs) conf.input_removed(p);
    for (const auto& p : removed.outputs) conf.output_removed(p);
    for (const auto& p : added.inputs) conf.input_added(p);
    for (const auto& p : added.outputs) conf.output_added(p);
  }
};

// Each vector describes one endpoint and one direction, so only group indices
// distinguish its members. Always replace metadata, even on a silent initial seed.
inline group_changes replace_groups(
    group_ports& current, group_ports next, const libremidi::observer_configuration& conf,
    bool notify = true)
{
  group_changes changes;
  auto diff = [&](auto& old, const auto& fresh, auto& removed, auto& added,
                  const auto& on_remove, const auto& on_add) {
    auto mask = [](const auto& ports) {
      unsigned bits{};
      for (const auto& p : ports) bits |= 1u << p.port;
      return bits;
    };
    if (on_add)
    {
      auto previous = mask(old);
      for (const auto& p : fresh)
        if (!(previous & (1u << p.port))) added.push_back(p);
    }
    if (on_remove)
    {
      auto present = mask(fresh);
      for (auto& p : old)
        if (!(present & (1u << p.port))) removed.push_back(std::move(p));
    }
  };
  if (notify)
  {
    diff(current.inputs, next.inputs, changes.removed.inputs, changes.added.inputs,
         conf.input_removed, conf.input_added);
    diff(current.outputs, next.outputs, changes.removed.outputs, changes.added.outputs,
         conf.output_removed, conf.output_added);
  }
  current = std::move(next);
  return changes;
}
}
