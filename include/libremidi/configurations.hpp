#pragma once

#include <libremidi/backends/alsa_raw/config.hpp>
#include <libremidi/backends/alsa_raw_ump/config.hpp>
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq_ump/config.hpp>
#include <libremidi/backends/coremidi/config.hpp>
#include <libremidi/backends/coremidi_ump/config.hpp>
#include <libremidi/backends/emscripten/config.hpp>
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/backends/keyboard/config.hpp>
#include <libremidi/backends/pipewire/config.hpp>
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winuwp/config.hpp>

namespace libremidi
{

struct dummy_configuration
{
};
}
