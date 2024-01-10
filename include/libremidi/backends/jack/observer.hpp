#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/backends/jack/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <unordered_set>

namespace libremidi
{
class observer_jack final
    : public observer_api
    , private jack_client
    , private error_handler
{
public:
  struct
      : observer_configuration
      , jack_observer_configuration
  {
  } configuration;

  explicit observer_jack(observer_configuration&& conf, jack_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    // Initialize JACK client
    if (configuration.context)
    {
      this->client = configuration.context;
      set_callbacks();
    }
    else
    {
      jack_status_t status{};
      this->client
          = jack_client_open(configuration.client_name.c_str(), JackNoStartServer, &status);
      if (status != jack_status_t{})
        warning(configuration, "observer_jack: " + std::to_string((int)jack_status_t{}));

      if (this->client != nullptr)
      {
        set_callbacks();

        jack_activate(this->client);
      }
    }
  }

  void initial_callback()
  {
    {
      const char** ports
          = jack_get_ports(client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);

      if (ports != nullptr)
      {
        int i = 0;
        while (ports[i] != nullptr)
        {
          auto port = jack_port_by_name(client, ports[i]);
          auto flags = jack_port_flags(port);

          bool physical = flags & JackPortIsPhysical;
          bool ok = false;
          if (configuration.track_hardware)
            ok |= physical;
          if (configuration.track_virtual)
            ok |= !physical;

          if (ok)
          {
            seen_input_ports.insert(ports[i]);
            if (this->configuration.input_added && configuration.notify_in_constructor)
              this->configuration.input_added(to_port_info<true>(client, port));
          }
          i++;
        }
      }

      jack_free(ports);
    }

    {
      const char** ports
          = jack_get_ports(client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);

      if (ports != nullptr)
      {
        int i = 0;
        while (ports[i] != nullptr)
        {
          auto port = jack_port_by_name(client, ports[i]);
          auto flags = jack_port_flags(port);

          bool physical = flags & JackPortIsPhysical;
          bool ok = false;
          if (configuration.track_hardware)
            ok |= physical;
          if (configuration.track_virtual)
            ok |= !physical;

          if (ok)
          {
            seen_output_ports.insert(ports[i]);
            if (this->configuration.output_added && configuration.notify_in_constructor)
              this->configuration.output_added(to_port_info<false>(client, port));
          }
          i++;
        }
      }

      jack_free(ports);
    }
  }

  void on_port_callback(jack_port_t* port, bool reg)
  {
    auto flags = jack_port_flags(port);
    std::string name = jack_port_name(port);
    if (reg)
    {
      std::string_view type = jack_port_type(port);
      if (type != JACK_DEFAULT_MIDI_TYPE)
        return;

      bool physical = flags & JackPortIsPhysical;
      bool ok = false;
      if (configuration.track_hardware)
        ok |= physical;
      if (configuration.track_virtual)
        ok |= !physical;
      if (!ok)
        return;

      // Note: we keep track of the ports as
      // when disconnecting, jack_port_type and jack_port_flags aren't correctly
      // set anymore.

      if (flags & JackPortIsOutput)
      {
        seen_input_ports.insert(name);
        if (this->configuration.input_added)
          this->configuration.input_added(to_port_info<true>(client, port));
      }
      else if (flags & JackPortIsInput)
      {
        seen_output_ports.insert(name);
        if (this->configuration.output_added)
          this->configuration.output_added(to_port_info<false>(client, port));
      }
    }
    else
    {
      if (auto it = seen_input_ports.find(name); it != seen_input_ports.end())
      {
        if (this->configuration.input_removed)
          this->configuration.input_removed(to_port_info<true>(client, port));
        seen_input_ports.erase(it);
      }
      if (auto it = seen_output_ports.find(name); it != seen_output_ports.end())
      {
        if (this->configuration.output_removed)
          this->configuration.output_removed(to_port_info<false>(client, port));
        seen_output_ports.erase(it);
      }
    }
  }

  void set_callbacks()
  {
    initial_callback();

    if (!configuration.has_callbacks())
      return;

    jack_set_port_registration_callback(
        this->client,
        +[](jack_port_id_t p, int r, void* arg) {
          auto& self = *(observer_jack*)arg;
          if (auto port = jack_port_by_id(self.client, p))
          {
            self.on_port_callback(port, r != 0);
          }
        },
        this);

    jack_set_port_rename_callback(
        this->client,
        +[](jack_port_id_t p, const char* /*old_name*/, const char* /*new_name*/, void* arg) {
          const auto& self = *static_cast<observer_jack*>(arg);

          auto port = jack_port_by_id(self.client, p);
          if (!port)
            return;
        },
        this);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::JACK_MIDI; }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    return get_ports<true>(this->client, nullptr, JackPortIsOutput);
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    return get_ports<false>(this->client, nullptr, JackPortIsInput);
  }

  ~observer_jack() { }

  std::unordered_set<std::string> seen_input_ports;
  std::unordered_set<std::string> seen_output_ports;
};
}
