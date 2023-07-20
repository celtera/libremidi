#pragma once
#include <libremidi/libremidi.hpp>

namespace libremidi
{

struct jack_input_configuration
{
  std::string client_name;
};

struct jack_output_configuration
{
  std::string client_name;

  int32_t ringbuffer_size = 16384;
};

}
