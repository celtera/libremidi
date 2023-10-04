#pragma once
#include <libremidi/backends/jack/midi_in.hpp>
#include <libremidi/backends/jack/midi_out.hpp>
#include <libremidi/backends/jack/observer.hpp>

//*********************************************************************//
//  API: UNIX JACK
//
//  Written primarily by Alexander Svetalkin, with updates for delta
//  time by Gary Scavone, April 2011.
//
//  *********************************************************************//

namespace libremidi
{
struct jack_backend
{
  using midi_in = midi_in_jack;
  using midi_out = midi_out_jack;
  using midi_observer = observer_jack;
  using midi_in_configuration = jack_input_configuration;
  using midi_out_configuration = jack_output_configuration;
  using midi_observer_configuration = jack_observer_configuration;
  static const constexpr auto API = libremidi::API::JACK_MIDI;
  static const constexpr auto name = "jack";
  static const constexpr auto display_name = "JACK";

  static inline bool available() noexcept
  {
#if LIBREMIDI_WEAKJACK
    return WeakJack::instance().available() == 0;
#else
    return true;
#endif
  }
};
}
