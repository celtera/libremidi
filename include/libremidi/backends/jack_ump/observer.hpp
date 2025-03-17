#pragma once
#include <libremidi/backends/jack/helpers.hpp>
#include <libremidi/backends/jack_ump/config.hpp>
#include <libremidi/detail/observer.hpp>

#include <unordered_set>

namespace libremidi::jack_ump
{
class observer_jack final
    : public observer_api
    , private jack_client
    , private jack_midi1 // seems like a bug in PipeWire? MIDI 2 flag is set but the MIDI 1 type string is used
    , private error_handler
{
public:
  struct
      : libremidi::observer_configuration
      , jack_ump::observer_configuration
  {
  } configuration;

  explicit observer_jack(
      libremidi::observer_configuration&& conf, jack_ump::observer_configuration&& apiconf)
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
        libremidi_handle_error(configuration, std::to_string((int)status));

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
      const char** ports = jack_get_ports(client, nullptr, port_type, JackPortIsOutput);

      if (ports != nullptr)
      {
        int i = 0;
        while (ports[i] != nullptr)
        {
          const auto port = jack_port_by_name(client, ports[i]);
          const auto flags = jack_port_flags(port);

          if (!(flags & 0x20)) // midi 2 check
          {
            i++;
            continue;
          }

          bool physical = flags & JackPortIsPhysical;
          bool ok = configuration.track_any;
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
      const char** ports = jack_get_ports(client, nullptr, port_type, JackPortIsInput);

      if (ports != nullptr)
      {
        int i = 0;
        while (ports[i] != nullptr)
        {
          const auto port = jack_port_by_name(client, ports[i]);
          const auto flags = jack_port_flags(port);

          if (!(flags & 0x20)) // midi 2 check
          {
            i++;
            continue;
          }

          bool physical = flags & JackPortIsPhysical;
          bool ok = configuration.track_any;
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
      if (type != port_type)
        return;

      if (!(flags & 0x20)) // midi 2 check
        return;

      bool physical = flags & JackPortIsPhysical;
      bool ok = configuration.track_any;
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

    jack_set_port_registration_callback(this->client, +[](jack_port_id_t p, int r, void* arg) {
      auto& self = *(observer_jack*)arg;
      if (auto port = jack_port_by_id(self.client, p))
      {
        self.on_port_callback(port, r != 0);
      }
    }, this);

    jack_set_port_rename_callback(
        this->client,
        +[](jack_port_id_t p, const char* /*old_name*/, const char* /*new_name*/, void* arg) {
      const auto& self = *static_cast<observer_jack*>(arg);

      auto port = jack_port_by_id(self.client, p);
      if (!port)
        return;
    }, this);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::JACK_UMP; }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    return get_ports<true>(this->client, nullptr, port_type, JackPortIsOutput, true);
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    return get_ports<false>(this->client, nullptr, port_type, JackPortIsInput, true);
  }

  ~observer_jack()
  {
    if (client && !configuration.context)
    {
      // If we own the client, deactivate it
      jack_deactivate(this->client);
      jack_client_close(this->client);
      this->client = nullptr;
    }
  }

  std::unordered_set<std::string> seen_input_ports;
  std::unordered_set<std::string> seen_output_ports;
};
}
