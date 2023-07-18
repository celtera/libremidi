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
#include <libremidi/detail/semaphore.hpp>
#include <libremidi/libremidi.hpp>

namespace libremidi
{

struct jack_helpers
{
  static bool check_port_name_length(
      const midi_api& self, std::string_view clientName, std::string_view portName)
  {
    // full name: "client_name:port_name\0"
    if (clientName.size() + portName.size() + 1 + 1 >= jack_port_name_size())
    {
      self.error<invalid_use_error>("JACK: port name length limit exceeded");
      return false;
    }
    return true;
  }

  static std::string
  get_port_name(const midi_api& self, const char** ports, unsigned int portNumber)
  {
    // Check port validity
    if (ports == nullptr)
    {
      self.warning("midi_jack::get_port_name: no ports available!");
      return {};
    }

    for (int i = 0; i <= portNumber; i++)
    {
      if (ports[i] == nullptr)
      {
        self.error<invalid_parameter_error>(
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
