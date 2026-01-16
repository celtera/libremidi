#pragma once
#include <libremidi/backends/alsa_raw/observer.hpp>
#include <libremidi/backends/alsa_raw_ump/endpoint.hpp>
#include <libremidi/backends/alsa_raw_ump/endpoint_config.hpp>
#include <libremidi/backends/alsa_raw_ump/endpoint_observer.hpp>
#include <libremidi/backends/alsa_raw_ump/midi_in.hpp>
#include <libremidi/backends/alsa_raw_ump/midi_out.hpp>
#include <libremidi/backends/alsa_raw_ump/observer.hpp>
#include <libremidi/backends/dummy.hpp>

#include <string_view>

NAMESPACE_LIBREMIDI::alsa_raw_ump
{
struct backend
{
  // MIDI 1.0 style API (separate input/output)
  using midi_in = midi_in_impl;
  using midi_out = midi_out_impl;
  using midi_observer = observer_impl;
  using midi_in_configuration = alsa_raw_ump::input_configuration;
  using midi_out_configuration = alsa_raw_ump::output_configuration;
  using midi_observer_configuration = alsa_raw_ump::observer_configuration;

  // MIDI 2.0 style API (bidirectional endpoints)
  using midi_endpoint = void;
  using midi_endpoint_configuration = void;
  // using midi_endpoint = endpoint_impl_threaded;
  // using midi_endpoint_configuration = alsa_raw_ump::endpoint_api_configuration;
  // using midi_endpoint_observer_configuration = alsa_raw_ump::endpoint_observer_api_configuration;

  static const constexpr auto API = libremidi::API::ALSA_RAW_UMP;
  static const constexpr std::string_view name = "alsa_raw_ump";
  static const constexpr std::string_view display_name = "ALSA (raw UMP)";

  static inline bool available() noexcept
  {
    static const libasound& snd = libasound::instance();
    return snd.available && snd.rawmidi.available && snd.ump.available;
  }
};
}
