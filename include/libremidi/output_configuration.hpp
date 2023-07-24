#pragma once
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>

#include <string>

namespace libremidi
{
struct output_configuration
{
  midi_error_callback on_error{};
  midi_error_callback on_warning{};
};
}
