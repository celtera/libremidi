#pragma once
#include <libremidi/backends/alsa_raw/config.hpp>

extern "C" typedef struct _snd_seq snd_seq_t;

namespace libremidi::alsa_seq
{

// Possible parameters:
// - Direct / non-direct output
// - Timer source used (high resolution, etc)
// - Timestamping
// - Tempo, ppq?

struct input_configuration
{
  std::string client_name = "libremidi client";
  snd_seq_t* context{};
  std::function<bool(const manual_poll_parameters&)> manual_poll;
};

struct output_configuration
{
  std::string client_name = "libremidi client";
  snd_seq_t* context{};
};

struct observer_configuration
{
  std::string client_name = "libremidi client";
  snd_seq_t* context{};
};

}
