#pragma once
#include <libremidi/backends/jack_ump/midi_in.hpp>
#include <libremidi/backends/jack_ump/midi_out.hpp>
#include <libremidi/backends/jack_ump/observer.hpp>

#include <string_view>

NAMESPACE_LIBREMIDI::jack_ump
{
struct backend
{
  using midi_in = midi_in_jack;
  using midi_out = midi_out_jack;
  using midi_observer = observer_jack;
  using midi_in_configuration = jack_ump::input_configuration;
  using midi_out_configuration = jack_ump::output_configuration;
  using midi_observer_configuration = jack_ump::observer_configuration;
  static const constexpr auto API = libremidi::API::JACK_UMP;
  static const constexpr std::string_view name = "jack_ump";
  static const constexpr std::string_view display_name = "JACK (UMP)";

  // Any JACK server carries UMP: the ports are MIDI ports with the MIDI 2 flag set,
  // and the packets go through unchanged. Servers that know the flag (PipeWire 1.4 and
  // later) also convert for MIDI 1 clients; with the others, such as jack2, a MIDI 1
  // client that connects to one of these ports gets the UMP bytes as they are.
  // Whether to connect is left to the clients.
  static inline bool available() noexcept { return libjack::instance().available; }
};
}
