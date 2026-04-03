#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/ump_endpoint_configuration.hpp>

#include <chrono>
#include <functional>

namespace libremidi::alsa_seq_ump
{

/// Configuration specific to ALSA Seq UMP endpoints
struct endpoint_api_configuration
{
  /// Optional: reuse an existing snd_seq_t context
  snd_seq_t* context{};

  /// Manual polling callback for integration with external event loops
  /// If set, no internal thread is created for message reception
  std::function<bool(const alsa_seq::poll_parameters&)> manual_poll;

  /// Callback to stop manual polling
  std::function<bool(snd_seq_addr_t)> stop_poll;

  /// Polling period when using internal thread
  std::chrono::milliseconds poll_period{2};
};

/// Configuration specific to ALSA Seq UMP endpoint observer
struct endpoint_observer_api_configuration
{
  /// Optional: reuse an existing snd_seq_t context
  snd_seq_t* context{};

  /// Manual polling callback for integration with external event loops
  std::function<bool(const libremidi::alsa_seq::poll_parameters&)> manual_poll;

  /// Callback to stop manual polling
  std::function<bool(snd_seq_addr_t)> stop_poll;

  /// Polling period when using internal thread
  std::chrono::milliseconds poll_period{100};
};

} // namespace libremidi::alsa_seq_ump
