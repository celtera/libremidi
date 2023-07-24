#pragma once
//*********************************************************************//
//  API: LINUX ALSA SEQUENCER
//*********************************************************************//

// API information found at:
//   - http://www.alsa-project.org/documentation.php#Library

// The ALSA Sequencer API is based on the use of a callback function for
// MIDI input.
//
// Thanks to Pedro Lopez-Cabanillas for help with the ALSA sequencer
// time stamps and other assorted fixes!!!

// If you don't need timestamping for incoming MIDI events, define the
// preprocessor definition LIBREMIDI_ALSA_AVOID_TIMESTAMPING to save resources
// associated with the ALSA sequencer queues.

#include <libremidi/backends/alsa_seq/midi_in.hpp>
#include <libremidi/backends/alsa_seq/midi_out.hpp>
#include <libremidi/backends/alsa_seq/observer.hpp>

namespace libremidi
{

struct alsa_backend
{
  using midi_in = midi_in_alsa;
  using midi_out = midi_out_alsa;
  using midi_observer = observer_alsa;
  using midi_in_configuration = alsa_sequencer_input_configuration;
  using midi_out_configuration = alsa_sequencer_output_configuration;
  using midi_observer_configuration = alsa_sequencer_observer_configuration;
  static const constexpr auto API = libremidi::API::LINUX_ALSA;
  static const constexpr auto name = "alsa_seq";
  static const constexpr auto display_name = "ALSA (sequencer)";
};

}
