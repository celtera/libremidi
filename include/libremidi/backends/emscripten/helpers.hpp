#pragma once
#include <libremidi/libremidi.hpp>

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
