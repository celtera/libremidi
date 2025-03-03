#pragma once
#include <libremidi/config.hpp>

#include <string>

namespace libremidi::winmidi
{

struct input_configuration
{
  std::string client_name = "libremidi client";
};

struct output_configuration
{
  std::string client_name = "libremidi client";
};

struct observer_configuration
{
  std::string client_name = "libremidi client";
};

}
