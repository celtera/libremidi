#pragma once
#include <libremidi/api.hpp>
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>
#include <libremidi/ump_endpoint_configuration.hpp>
#include <libremidi/ump_endpoint_info.hpp>

#include <string_view>

namespace libremidi
{

//=============================================================================
// UMP Endpoint Observer API Base
//=============================================================================

/// Base class for UMP endpoint observer implementations.
/// Each backend (Windows MIDI, CoreMIDI UMP, ALSA UMP) implements this.
class ump_endpoint_observer_api
{
public:
  ump_endpoint_observer_api() = default;
  virtual ~ump_endpoint_observer_api() = default;

  ump_endpoint_observer_api(const ump_endpoint_observer_api&) = delete;
  ump_endpoint_observer_api(ump_endpoint_observer_api&&) = delete;
  ump_endpoint_observer_api& operator=(const ump_endpoint_observer_api&) = delete;
  ump_endpoint_observer_api& operator=(ump_endpoint_observer_api&&) = delete;

  /// Get the API identifier
  [[nodiscard]] virtual libremidi::API get_current_api() const noexcept = 0;

  /// Get all discovered endpoints
  [[nodiscard]] virtual std::vector<ump_endpoint_info> get_endpoints() const noexcept = 0;

  /// Trigger a refresh of endpoint information
  virtual stdx::error refresh() noexcept = 0;

  /// Check if the observer is in a valid state
  [[nodiscard]] stdx::error is_valid() const noexcept { return valid_; }

protected:
  stdx::error valid_{std::errc::not_connected};
};

//=============================================================================
// UMP Endpoint API Base
//=============================================================================

class ump_connection_api
{
};

/// Base class for bidirectional UMP endpoint implementations.
/// Each backend implements this for both sending and receiving.
class ump_endpoint_api
{
public:
  ump_endpoint_api() = default;
  virtual ~ump_endpoint_api() = default;

  ump_endpoint_api(const ump_endpoint_api&) = delete;
  ump_endpoint_api(ump_endpoint_api&&) = delete;
  ump_endpoint_api& operator=(const ump_endpoint_api&) = delete;
  ump_endpoint_api& operator=(ump_endpoint_api&&) = delete;

  //-------------------------------------------------------------------------
  // API Identity
  //-------------------------------------------------------------------------

  [[nodiscard]] virtual libremidi::API get_current_api() const noexcept = 0;

  //-------------------------------------------------------------------------
  // Connection Management
  //-------------------------------------------------------------------------

  /// Open a connection to a UMP endpoint
  virtual stdx::error open(
      const ump_endpoint_info& endpoint,
      std::string_view local_name) = 0;

  /// Create and open a virtual endpoint
  virtual stdx::error open_virtual(std::string_view port_name)
  {
    return std::errc::function_not_supported;
  }

  /// Close the connection
  virtual stdx::error close() = 0;

  /// Get current endpoint info (may be updated since opening)
  [[nodiscard]] virtual std::optional<ump_endpoint_info> get_endpoint_info() const noexcept
  {
    return connected_endpoint_;
  }

  /// Set port name if supported
  virtual stdx::error set_port_name(std::string_view name)
  {
    return std::errc::function_not_supported;
  }

  //-------------------------------------------------------------------------
  // Sending
  //-------------------------------------------------------------------------

  /// Send UMP words
  virtual stdx::error send_ump(const uint32_t* words, size_t count) = 0;

  /// Schedule UMP for future delivery (optional)
  virtual stdx::error schedule_ump(int64_t timestamp, const uint32_t* words, size_t count)
  {
    // Default: immediate send, ignore timestamp
    return send_ump(words, count);
  }

  /// Get current time in endpoint's domain
  [[nodiscard]] virtual int64_t current_time() const noexcept { return 0; }

  //-------------------------------------------------------------------------
  // Protocol and Capabilities
  //-------------------------------------------------------------------------

  /// Get currently active protocol
  [[nodiscard]] virtual midi_protocol get_active_protocol() const noexcept
  {
    return active_protocol_;
  }

  /// Request protocol change
  virtual stdx::error request_protocol(midi_protocol protocol)
  {
    return std::errc::function_not_supported;
  }

  /// Check if JR timestamps are enabled
  [[nodiscard]] virtual bool jr_timestamps_enabled() const noexcept
  {
    return jr_timestamps_enabled_;
  }

  //-------------------------------------------------------------------------
  // Function Blocks
  //-------------------------------------------------------------------------

  /// Get current function blocks
  [[nodiscard]] virtual std::vector<function_block_info> get_function_blocks() const noexcept
  {
    if (connected_endpoint_)
      return connected_endpoint_->function_blocks;
    return {};
  }

  //-------------------------------------------------------------------------
  // MIDI-CI
  //-------------------------------------------------------------------------

  /// Send MIDI-CI discovery
  virtual stdx::error send_ci_discovery()
  {
    return std::errc::function_not_supported;
  }

  /// Check MIDI-CI support
  [[nodiscard]] virtual bool supports_midi_ci() const noexcept
  {
    if (!connected_endpoint_)
      return false;
    for (const auto& fb : connected_endpoint_->function_blocks)
      if (fb.supports_midi_ci())
        return true;
    return false;
  }

  //-------------------------------------------------------------------------
  // State Queries
  //-------------------------------------------------------------------------

  [[nodiscard]] stdx::error is_client_open() const noexcept { return client_open_; }
  [[nodiscard]] bool is_port_open() const noexcept { return port_open_; }
  [[nodiscard]] bool is_connected() const noexcept { return connected_; }

protected:
  friend class ump_endpoint;

  stdx::error client_open_{std::errc::not_connected};
  bool port_open_{false};
  bool connected_{false};

  std::optional<ump_endpoint_info> connected_endpoint_{};
  midi_protocol active_protocol_{midi_protocol::midi1};
  bool jr_timestamps_enabled_{false};
};

template <typename T, typename Arg>
std::unique_ptr<ump_endpoint_api>
make(libremidi::remote_ump_endpoint_configuration&& conf, Arg&& arg)
{
  return std::make_unique<T>(std::move(conf), std::move(arg));
}

}
