#pragma once
#include <libremidi/backends/coreaudio/midi_in.hpp>
#include <libremidi/backends/coreaudio/midi_out.hpp>
#include <libremidi/backends/coreaudio/observer.hpp>

namespace libremidi
{
struct core_backend
{
  using midi_in = midi_in_core;
  using midi_out = midi_out_core;
  using midi_observer = observer_core;
  using midi_in_configuration = coremidi_input_configuration;
  using midi_out_configuration = coremidi_output_configuration;
  using midi_observer_configuration = coremidi_observer_configuration;
  static const constexpr auto API = libremidi::API::MACOSX_CORE;
  static const constexpr auto name = "core";
  static const constexpr auto display_name = "CoreMIDI";
};
}

namespace libremidi::coremidi_ump
{
struct backend
{
  using midi_in = midi_in_impl;
  using midi_out = midi_out_core;
  using midi_observer = observer_core;
  using midi_in_configuration = coremidi_input_configuration;
  using midi_out_configuration = coremidi_output_configuration;
  using midi_observer_configuration = coremidi_observer_configuration;
  static const constexpr auto API = libremidi::API::COREMIDI_UMP;
  static const constexpr auto name = "core_ump";
  static const constexpr auto display_name = "CoreMIDI UMP";
};
}
#if TARGET_OS_IPHONE
  #undef AudioGetCurrentHostTime
#endif
