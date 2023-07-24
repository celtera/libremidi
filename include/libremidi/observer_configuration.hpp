#pragma once
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>

#include <string>

namespace libremidi
{
struct observer_configuration
{
  midi_error_callback on_error{};
  midi_error_callback on_warning{};

  std::function<void(int, std::string)> input_added;
  std::function<void(int, std::string)> input_removed;
  std::function<void(int, std::string)> output_added;
  std::function<void(int, std::string)> output_removed;
};
}
