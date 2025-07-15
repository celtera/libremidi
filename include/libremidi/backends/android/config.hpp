#pragma once
#include <libremidi/config.hpp>

namespace libremidi
{
namespace android
{
struct input_configuration
{
  std::string_view client_name = "libremidi client";
};

struct output_configuration
{
  std::string_view client_name = "libremidi client";
};

struct observer_configuration
{
  std::string_view client_name = "libremidi client";
};
}
}