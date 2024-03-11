#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/detail/observer.hpp>
;
namespace libremidi::winmidi
{
struct port_info
{
  hstring id;
  hstring name;
};

class observer_impl final : public observer_api
{
public:
  struct
      : libremidi::observer_configuration
      , winmidi::observer_configuration
  {
  } configuration;


  explicit observer_impl(
      libremidi::observer_configuration&& conf, winmidi::observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
      , session{MidiSession::CreateSession(L"libremidi session")}
  {
    if (!configuration.has_callbacks())
      return;

    if (configuration.notify_in_constructor)
    {
      if (configuration.input_added)
        for (const auto& p : get_input_ports())
          configuration.input_added(p);

      if (configuration.output_added)
        for (const auto& p : get_output_ports())
          configuration.output_added(p);
    }

    /*
    evTokenOnInputAdded_
        = internalInPortObserver_.PortAdded([this](const port_info& p) { on_input_added(p); });
    evTokenOnInputRemoved_
        = internalInPortObserver_.PortRemoved([this](const port_info& p) { on_input_removed(p); });
    evTokenOnOutputAdded_
        = internalOutPortObserver_.PortAdded([this](const port_info& p) { on_output_added(p); });
    evTokenOnOutputRemoved_ = internalOutPortObserver_.PortRemoved(
        [this](const port_info& p) { on_output_removed(p); });
*/
  }

  ~observer_impl()
  {
    if (!configuration.has_callbacks())
      return;
    // internalInPortObserver_.PortAdded(evTokenOnInputAdded_);
    // internalInPortObserver_.PortRemoved(evTokenOnInputRemoved_);
    // internalOutPortObserver_.PortAdded(evTokenOnOutputAdded_);
    // internalOutPortObserver_.PortRemoved(evTokenOnOutputRemoved_);
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::WINDOWS_MIDI_SERVICES;
  }

  template <bool Input>
  auto to_port_info(const DeviceInformation& p) const noexcept
      -> std::conditional_t<Input, input_port, output_port>
  {
    return {
        {.client = 0,
         .port = 0,
         .manufacturer = "",
         .device_name = "",
         .port_name = to_string(p.Id()),
         .display_name = to_string(p.Name())}};
  }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    std::vector<libremidi::input_port> ret;

    auto deviceSelector = MidiEndpointConnection::GetDeviceSelector();
    auto endpointDevices = DeviceInformation::FindAllAsync(deviceSelector).get();
    for (const auto& ep : endpointDevices)
    {
      if(ep.Name().starts_with(L"Diagnostics")) {
        continue;
      }
      // FIXME if(has input...)

      ret.emplace_back(to_port_info<true>(ep));
    }

    return ret;
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    std::vector<libremidi::output_port> ret;

    auto deviceSelector = MidiEndpointConnection::GetDeviceSelector();
    auto endpointDevices = DeviceInformation::FindAllAsync(deviceSelector).get();
    for (const auto& ep : endpointDevices)
    {
      if(ep.Name().starts_with(L"Diagnostics")) {
        continue;
      }
      // FIXME if(has output...)
      ret.emplace_back(to_port_info<false>(ep));
    }

    return ret;
  }

  void on_input_added(const DeviceInformation& name)
  {
    if (configuration.input_added)
      configuration.input_added(to_port_info<true>(name));
  }

  void on_input_removed(const DeviceInformation& name)
  {
    if (configuration.input_removed)
      configuration.input_removed(to_port_info<true>(name));
  }

  void on_output_added(const DeviceInformation& name)
  {
    if (configuration.output_added)
      configuration.output_added(to_port_info<false>(name));
  }

  void on_output_removed(const DeviceInformation& name)
  {
    if (configuration.output_removed)
      configuration.output_removed(to_port_info<false>(name));
  }

private:
  MidiSession session;
};

}
