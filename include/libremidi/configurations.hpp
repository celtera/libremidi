#pragma once

#if defined(__linux__)
  #include <libremidi/backends/alsa_raw/config.hpp>
  #include <libremidi/backends/alsa_raw_ump/config.hpp>
  #include <libremidi/backends/alsa_seq/config.hpp>
  #include <libremidi/backends/alsa_seq_ump/config.hpp>
  #include <libremidi/backends/pipewire/config.hpp>
#endif

#if defined(__APPLE__)
  #include <libremidi/backends/coremidi/config.hpp>
  #include <libremidi/backends/coremidi_ump/config.hpp>
#endif

#if defined(_WIN32)
  #include <libremidi/backends/winmidi/config.hpp>
  #include <libremidi/backends/winmm/config.hpp>
  #include <libremidi/backends/winuwp/config.hpp>
#endif

#if defined(__EMSCRIPTEN__)
  #include <libremidi/backends/emscripten/config.hpp>
#endif

#include <libremidi/backends/jack/config.hpp>

namespace libremidi
{

struct dummy_configuration
{
};

}
