#pragma once
#include <libremidi/backends/coremidi_ump/midi_in.hpp>
#include <libremidi/backends/coremidi_ump/midi_out.hpp>
#include <libremidi/backends/coremidi_ump/observer.hpp>

namespace libremidi::coremidi_ump
{
struct backend
{
  using midi_in = midi_in_impl;
  using midi_out = midi_out_impl;
  using midi_observer = observer_impl;
  using midi_in_configuration = coremidi_input_configuration;
  using midi_out_configuration = coremidi_output_configuration;
  using midi_observer_configuration = coremidi_observer_configuration;
  static const constexpr auto API = libremidi::API::COREMIDI_UMP;
  static const constexpr auto name = "core_ump";
  static const constexpr auto display_name = "CoreMIDI UMP";

  static constexpr inline bool available() noexcept { return true; /* todo? */ }
};
}
