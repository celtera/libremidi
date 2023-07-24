#pragma once
#include <libremidi/config.hpp>

namespace libremidi
{

struct winmm_input_configuration
{
  int sysex_buffer_size = 1024;
  int sysex_buffer_count = 4;
};

struct winmm_output_configuration
{
};

struct winmm_observer_configuration
{
  int poll_period = 100; // ms
};

}
