#pragma once
#include <libremidi/config.hpp>

#include <string>

namespace libremidi
{
namespace webmidi_helpers
{
struct device_information
{
  std::string id;
  std::string name;
  bool connected{};
};
}
}
