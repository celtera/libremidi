#pragma once
#include <libremidi/backends/alsa_raw/midi_in.hpp>
#include <libremidi/backends/alsa_raw/midi_out.hpp>
#include <libremidi/backends/alsa_raw/observer.hpp>

// Credits: greatly inspired from
// https://ccrma.stanford.edu/~craig/articles/linuxmidi/alsa-1.0/alsarawmidiout.c
// https://ccrma.stanford.edu/~craig/articles/linuxmidi/alsa-1.0/alsarawportlist.c
// Thanks Craig Stuart Sapp <craig@ccrma.stanford.edu>

namespace libremidi
{
struct raw_alsa_backend
{
  using midi_in = midi_in_raw_alsa;
  using midi_out = midi_out_raw_alsa;
  using midi_in_configuration = alsa_raw_input_configuration;
  using midi_out_configuration = alsa_raw_output_configuration;
  using midi_observer = observer_alsa_raw;
  static const constexpr auto API = libremidi::API::LINUX_ALSA_RAW;
  static const constexpr auto name = "alsa_raw";
  static const constexpr auto display_name = "ALSA (raw)";
};
}
