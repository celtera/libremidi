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
#include <libremidi/libremidi.hpp>

namespace libremidi
{

struct jack_helpers
{
  jack_client_t* client{};
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

  static bool
  check_port_name_length(const auto& self, std::string_view clientName, std::string_view portName)
  {
    // full name: "client_name:port_name\0"
    if (clientName.size() + portName.size() + 1 + 1 >= jack_port_name_size())
    {
      self.template error<invalid_use_error>(self.configuration, "JACK: port name length limit exceeded");
      return false;
    }
    return true;
  }

  static std::string
  get_port_name(const auto& self, const char** ports, unsigned int portNumber)
  {
    // Check port validity
    if (ports == nullptr)
    {
      self.warning(self.configuration, "midi_jack::get_port_name: no ports available!");
      return {};
    }

    for (int i = 0; i <= portNumber; i++)
    {
      if (ports[i] == nullptr)
      {
        self.template error<invalid_parameter_error>(self.configuration,
            "midi_jack::get_port_name: invalid 'portNumber' argument: "
            + std::to_string(portNumber));
        return {};
      }

      if (i == portNumber)
      {
        return ports[portNumber];
      }
    }

    return {};
  }
};
}
