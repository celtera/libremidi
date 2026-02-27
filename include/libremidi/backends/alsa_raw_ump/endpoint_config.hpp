#pragma once
#include <libremidi/backends/alsa_raw/config.hpp>
#include <libremidi/ump_endpoint_configuration.hpp>

#include <chrono>
#include <functional>

namespace libremidi::alsa_raw_ump
{

/// Configuration specific to ALSA Raw UMP endpoints
struct endpoint_api_configuration
{
  /// Manual polling callback for integration with external event loops
  /// If set, no internal thread is created for message reception
  std::function<bool(const manual_poll_parameters&)> manual_poll;

  /// Polling period when using internal thread
  std::chrono::milliseconds poll_period{2};

  /// For large messages, chunk their content and wait
  std::optional<chunking_parameters> chunking;
};

/// Configuration specific to ALSA Raw UMP endpoint observer
struct endpoint_observer_api_configuration : public alsa_raw_observer_configuration
{
};

} // namespace libremidi::alsa_raw_ump
