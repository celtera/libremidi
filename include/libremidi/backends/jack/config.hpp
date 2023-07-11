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
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/detail/semaphore.hpp>
#include <libremidi/libremidi.hpp>

#include <ostream>
#include <sstream>

namespace libremidi
{
struct jack_data
{
  static const constexpr auto ringbuffer_size = 16384;
  jack_client_t* client{};
  jack_port_t* port{};
  jack_ringbuffer_t* buffSize{};
  jack_ringbuffer_t* buffMessage{};
  jack_time_t lastTime{};

  libremidi::semaphore sem_cleanup;
  libremidi::semaphore sem_needpost{};

  midi_in_api::in_data* rtMidiIn{};
};

struct jack_helpers
{
  static bool check_port_name_length(
      midi_api& self, std::string_view clientName, std::string_view portName) noexcept
  {
    // full name: "client_name:port_name\0"
    if (clientName.size() + portName.size() + 1 + 1 >= jack_port_name_size())
    {
      self.error<invalid_use_error>("JACK: port name length limit exceeded");
      return false;
    }
    return true;
  }
};
}
