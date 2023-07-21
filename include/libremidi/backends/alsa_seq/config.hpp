#pragma once
#include <libremidi/libremidi.hpp>

namespace libremidi
{

// Possible parameters:
// - Direct / non-direct output
// - Timer source used (high resolution, etc)
// - Timestamping
// - Tempo, ppq?

struct alsa_sequencer_input_configuration
{
  std::string client_name;
};

struct alsa_sequencer_output_configuration
{
  std::string client_name;
};

}
