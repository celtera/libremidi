#pragma once
#include <libremidi/config.hpp>

#include <chrono>

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
  std::chrono::milliseconds poll_period{100};
};

}
