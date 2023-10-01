#pragma once
#include <libremidi/backends/winmidi/midi_in.hpp>
#include <libremidi/backends/winmidi/midi_out.hpp>
#include <libremidi/backends/winmidi/observer.hpp>

namespace libremidi::winmidi
{
struct backend
{
  using midi_in = midi_in_impl;
  using midi_out = midi_out_impl;
  using midi_observer = observer_impl;
  using midi_in_configuration = winmidi::input_configuration;
  using midi_out_configuration = winmidi::output_configuration;
  using midi_observer_configuration = winmidi::observer_configuration;
  static const constexpr auto API = libremidi::API::WINDOWS_MIDI_SERVICES;
  static const constexpr auto name = "winmidi";
  static const constexpr auto display_name = "Windows MIDI Services";

  static constexpr inline bool available() noexcept { return true; }
};
} // namespace libremidi
