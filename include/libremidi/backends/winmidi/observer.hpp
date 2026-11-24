#pragma once
#include <map>
#include <mutex>
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/detail/observer.hpp>

NAMESPACE_LIBREMIDI::winmidi
{

inline auto find_standard_endpoints()
{
  return MidiEndpointDeviceInformation::FindAll(
      MidiEndpointDeviceInformationSortOrder::Name,
      MidiEndpointDeviceInformationFilters::AllStandardEndpoints);
}

inline group_ports project_ports(
    const MidiEndpointDeviceInformation& ep, const libremidi::observer_configuration& conf,
    bool inputs = true, bool outputs = true)
{
  const auto info = ep.GetTransportSuppliedInfo();
  const auto type = info ? transport_from_code(to_string(info.TransportCode())) : transport_type::unknown;
  if (!accepts_endpoint(conf, type))
    return {};

  const port_information base{
      .api = API::WINDOWS_MIDI_SERVICES,
      .container = std::bit_cast<libremidi::uuid>(ep.ContainerId()),
      .device = to_string(ep.EndpointDeviceId()),
      .manufacturer = info ? to_string(info.ManufacturerName()) : std::string{},
      .product = info ? to_string(info.Name()) : std::string{},
      .serial = info ? to_string(info.SerialNumber()) : std::string{},
      .device_name = to_string(ep.Name()),
      .type = type};

  return make_group_ports(ep, base, inputs, outputs, [](const auto& block) {
    return to_string(block.Name());
  });
}

// Device interface IDs are case insensitive; watcher removal args need not
// preserve the casing used by the endpoint-information object.
struct endpoint_id_less
{
  bool operator()(const winrt::hstring& a, const winrt::hstring& b) const noexcept
  {
    return CompareStringOrdinal(a.c_str(), a.size(), b.c_str(), b.size(), TRUE) == CSTR_LESS_THAN;
  }
};

struct watched_groups
{
  libremidi::observer_configuration conf;
  mutable std::mutex mutex;
  std::map<winrt::hstring, group_ports, endpoint_id_less> known;

  void replace(const winrt::hstring& id, group_ports next, bool notify = true)
  {
    group_changes changes;
    {
      std::lock_guard guard{mutex};
      auto it = known.find(id);
      if (it == known.end())
      {
        if (next.empty()) return;
        it = known.try_emplace(id).first;
      }
      changes = replace_groups(it->second, std::move(next), conf, notify);
      if (it->second.empty()) known.erase(it);
    }
    changes.notify(conf);
  }

  void update(const MidiEndpointDeviceInformation& ep, bool notify = true)
  {
    if (ep) replace(ep.EndpointDeviceId(), project_ports(ep, conf), notify);
  }

  void enumeration_completed(const MidiEndpointDeviceWatcher& watcher)
  {
    // An endpoint may have disappeared between our synchronous constructor
    // snapshot and Start(). It will not get an initial Added or Removed event.
    const auto enumerated = watcher.EnumeratedEndpointDevices();
    std::vector<winrt::hstring> missing;
    {
      std::lock_guard guard{mutex};
      for (const auto& [id, _] : known)
        if (!enumerated.HasKey(id)) missing.push_back(id);
    }
    for (const auto& id : missing) replace(id, {});
  }
};

class observer_impl final : public observer_api
{
  std::shared_ptr<watched_groups> state;
  MidiEndpointDeviceWatcher watcher{nullptr};
  MidiEndpointDeviceWatcher::Added_revoker added;
  MidiEndpointDeviceWatcher::Updated_revoker updated;
  MidiEndpointDeviceWatcher::Removed_revoker removed;
  MidiEndpointDeviceWatcher::EnumerationCompleted_revoker completed;

public:
  explicit observer_impl(libremidi::observer_configuration&& conf, winmidi::observer_configuration&&)
      : state{std::make_shared<watched_groups>()}
  {
    state->conf = std::move(conf);
    if (!state->conf.has_callbacks()) return;
    watcher = MidiEndpointDeviceWatcher::Create(MidiEndpointDeviceInformationFilters::AllStandardEndpoints);
    if (!watcher) return;

    added = watcher.Added(winrt::auto_revoke, [s = state](auto&&, const auto& args) {
      s->update(args.AddedDevice());
    });
    updated = watcher.Updated(winrt::auto_revoke, [s = state](auto&&, const auto& args) {
      // Read all metadata: a rename or transport update need not change blocks.
      s->update(args.UpdatedDevice());
    });
    removed = watcher.Removed(winrt::auto_revoke, [s = state](auto&&, const auto& args) {
      s->replace(args.RemovedDevice().EndpointDeviceId(), {});
    });
    completed = watcher.EnumerationCompleted(winrt::auto_revoke, [s = state](const auto& w, auto&&) {
      s->enumeration_completed(w);
    });

    // Seed even remove-only observers. Initial Added callbacks from Start()
    // diff against this snapshot rather than reporting existing ports twice.
    for (const auto& ep : find_standard_endpoints())
      state->update(ep, state->conf.notify_in_constructor);
    watcher.Start();
  }

  ~observer_impl() override
  {
    if (!watcher) return;
    added.revoke();
    updated.revoke();
    removed.revoke();
    completed.revoke();
    watcher.Stop();
  }

  libremidi::API get_current_api() const noexcept override { return API::WINDOWS_MIDI_SERVICES; }

  template <typename Port>
  auto list(std::vector<Port> group_ports::*member) const
  {
    std::vector<Port> result;
    if (watcher)
    {
      std::lock_guard guard{state->mutex};
      for (const auto& [_, snapshot] : state->known)
      {
        const auto& ports = snapshot.*member;
        result.insert(result.end(), ports.begin(), ports.end());
      }
    }
    else
    {
      constexpr bool input = std::is_same_v<Port, input_port>;
      for (const auto& ep : find_standard_endpoints())
      {
        auto snapshot = project_ports(ep, state->conf, input, !input);
        auto& ports = snapshot.*member;
        result.insert(result.end(), std::make_move_iterator(ports.begin()), std::make_move_iterator(ports.end()));
      }
    }
    return result;
  }

  std::vector<input_port> get_input_ports() const noexcept override { return list(&group_ports::inputs); }
  std::vector<output_port> get_output_ports() const noexcept override { return list(&group_ports::outputs); }
};

}
