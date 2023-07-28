#pragma once
#include <libremidi/backends/alsa_raw/config.hpp>

namespace libremidi::alsa_raw_ump
{
struct input_configuration
{
  std::function<bool(const manual_poll_parameters&)> manual_poll;
};

struct output_configuration
{
  /**
   * For large messages, chunk their content and wait.
   * Setting a null optional will disable chunking.
   */
  std::optional<chunking_parameters> chunking;
};

struct observer_configuration
{
  std::chrono::milliseconds poll_period{100};
};
}
