#pragma once
#include <libremidi/config.hpp>

#define RT_WINMM_OBSERVER_POLL_PERIOD_MS 100

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

}
