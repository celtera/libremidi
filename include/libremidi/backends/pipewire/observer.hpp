#pragma once
#include <libremidi/backends/pipewire/config.hpp>
#include <libremidi/backends/pipewire/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <unordered_set>

namespace libremidi
{
class observer_pipewire final
    : public observer_api
    , private pipewire_client
    , private error_handler
{
public:
  struct
      : observer_configuration
      , pipewire_observer_configuration
  {
  } configuration;

  explicit observer_pipewire(observer_configuration&& conf, pipewire_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    // Initialize PipeWire client
    if (configuration.context)
    {
      this->client = configuration.context;
      set_callbacks();
    }
    else
    {
      pipewire_status_t status{};
      this->client
          = pipewire_client_open(configuration.client_name.c_str(), PipewireNoStartServer, &status);
      if (status != pipewire_status_t{})
        warning(configuration, "observer_pipewire: " + std::to_string((int)pipewire_status_t{}));

      if (this->client != nullptr)
      {
        set_callbacks();

        pipewire_activate(this->client);
      }
    }
  }

  void initial_callback()
  {
    {
      const char** ports
          = pipewire_get_ports(client, nullptr, PipeWire_DEFAULT_MIDI_TYPE, PipewirePortIsOutput);

      if (ports != nullptr)
      {
        int i = 0;
        while (ports[i] != nullptr)
        {
          auto port = pipewire_port_by_name(client, ports[i]);
          auto flags = pipewire_port_flags(port);

          bool physical = flags & PipewirePortIsPhysical;
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

      pipewire_free(ports);
    }

    {
      const char** ports
          = pipewire_get_ports(client, nullptr, PipeWire_DEFAULT_MIDI_TYPE, PipewirePortIsInput);

      if (ports != nullptr)
      {
        int i = 0;
        while (ports[i] != nullptr)
        {
          auto port = pipewire_port_by_name(client, ports[i]);
          auto flags = pipewire_port_flags(port);

          bool physical = flags & PipewirePortIsPhysical;
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

      pipewire_free(ports);
    }
  }

  void on_port_callback(pipewire_port_t* port, bool reg)
  {
    auto flags = pipewire_port_flags(port);
    std::string name = pipewire_port_name(port);
    if (reg)
    {
      std::string_view type = pipewire_port_type(port);
      if (type != PipeWire_DEFAULT_MIDI_TYPE)
        return;

      bool physical = flags & PipewirePortIsPhysical;
      bool ok = false;
      if (configuration.track_hardware)
        ok |= physical;
      if (configuration.track_virtual)
        ok |= !physical;
      if (!ok)
        return;

      // Note: we keep track of the ports as
      // when disconnecting, pipewire_port_type and pipewire_port_flags aren't correctly
      // set anymore.

      if (flags & PipewirePortIsOutput)
      {
        seen_input_ports.insert(name);
        if (this->configuration.input_added)
          this->configuration.input_added(to_port_info<true>(client, port));
      }
      else if (flags & PipewirePortIsInput)
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

    pipewire_set_port_registration_callback(
        this->client,
        +[](pipewire_port_id_t p, int r, void* arg) {
          auto& self = *(observer_pipewire*)arg;
          if (auto port = pipewire_port_by_id(self.client, p))
          {
            self.on_port_callback(port, r != 0);
          }
        },
        this);

    pipewire_set_port_rename_callback(
        this->client,
        +[](pipewire_port_id_t p, const char* /*old_name*/, const char* /*new_name*/, void* arg) {
          const auto& self = *static_cast<observer_pipewire*>(arg);

          auto port = pipewire_port_by_id(self.client, p);
          if (!port)
            return;
        },
        this);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::PipeWire_MIDI; }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    return get_ports<true>(this->client, nullptr, PipewirePortIsOutput);
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    return get_ports<false>(this->client, nullptr, PipewirePortIsInput);
  }

  ~observer_pipewire()
  {
    if (client && !configuration.context)
    {
      // If we own the client, deactivate it
      pipewire_deactivate(this->client);
      pipewire_client_close(this->client);
      this->client = nullptr;
    }
  }

  std::unordered_set<std::string> seen_input_ports;
  std::unordered_set<std::string> seen_output_ports;
};
}
