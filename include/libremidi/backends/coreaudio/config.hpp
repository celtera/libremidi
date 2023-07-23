#pragma once
#include <libremidi/config.hpp>

#include <string>

namespace libremidi
{

struct coremidi_input_configuration
{
  std::string client_name;
};

struct coremidi_output_configuration
{
  std::string client_name;
};

}
