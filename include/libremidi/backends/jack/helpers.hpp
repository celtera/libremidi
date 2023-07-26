#pragma once

#if __has_include(<weakjack/weak_libjack.h>)
  #include <weakjack/weak_libjack.h>
#elif __has_include(<weak_libjack.h>)
  #include <weak_libjack.h>
#elif __has_include(<jack/jack.h>)
  #include <jack/jack.h>
  #include <jack/midiport.h>
  #include <jack/ringbuffer.h>
#endif
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
struct jack_client
{
  jack_client_t* client{};

  static std::vector<libremidi::port_information>
  get_ports(jack_client_t* client, JackPortFlags flags) noexcept
  {
    std::vector<libremidi::port_information> ret;

    if (!client)
      return {};

    const char** ports = jack_get_ports(client, nullptr, JACK_DEFAULT_MIDI_TYPE, flags);

    if (ports == nullptr)
      return {};

    int i = 0;
    while (ports[i] != nullptr)
    {
      auto port = jack_port_by_name(client, ports[i]);
      jack_port_short_name(port);

      ret.push_back(port_information{
          .client = std::uintptr_t(client),
          .port = 0,
          .manufacturer = "",
          .device_name = "",
          .port_name = ports[i],
          .display_name = ports[i],
      });

      i++;
    }

    jack_free(ports);

    return ret;
  }
};

struct jack_helpers : jack_client
{
  jack_port_t* port{};

  template <auto callback, typename Self>
  jack_status_t connect(Self& self)
  {
    auto& configuration = self.configuration;

    if (this->client)
      return jack_status_t{};

    // Initialize JACK client
    if (configuration.context)
    {
      if (!configuration.set_process_func)
        return JackFailure;
      configuration.set_process_func(
          [&self](jack_nframes_t nf) -> int { return (self.*callback)(nf); });

      this->client = configuration.context;
      return jack_status_t{};
    }
    else
    {
      jack_status_t status{};
      this->client
          = jack_client_open(configuration.client_name.c_str(), JackNoStartServer, &status);
      if (this->client != nullptr)
      {
        jack_set_process_callback(
            this->client,
            +[](jack_nframes_t nf, void* ctx) -> int {
              return (static_cast<Self*>(ctx)->*callback)(nf);
            },
            &self);
        jack_activate(this->client);
      }
      return status;
    }
  }

  bool create_local_port(const auto& self, std::string_view portName, JackPortFlags flags)
  {
    // full name: "client_name:port_name\0"
    if (self.configuration.client_name.size() + portName.size() + 1 + 1 >= jack_port_name_size())
    {
      self.template error<invalid_use_error>(
          self.configuration, "JACK: port name length limit exceeded");
      return false;
    }

    if (!this->port)
      this->port
          = jack_port_register(this->client, portName.data(), JACK_DEFAULT_MIDI_TYPE, flags, 0);

    if (!this->port)
    {
      self.template error<driver_error>(self.configuration, "JACK: error creating port");
      return false;
    }
    return true;
  }
};
}
