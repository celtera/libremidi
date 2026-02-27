#pragma once

#include <libremidi/api.hpp>
#include <libremidi/configurations.hpp>
#include <libremidi/error.hpp>
#include <libremidi/ump.hpp>
#include <libremidi/ump_endpoint_configuration.hpp>
#include <libremidi/ump_endpoint_info.hpp>

#include <memory>
#include <span>
#include <vector>

namespace libremidi
{

// Forward declarations
class ump_endpoint_api;
class ump_connection_api;
class ump_endpoint_observer_api;

/// Bidirectional MIDI 2.0 endpoint for sending and receiving UMP messages.
class LIBREMIDI_EXPORT ump_endpoint
{
public:
  /// Create an endpoint with the default MIDI 2.0 backend for the platform
  explicit ump_endpoint(const remote_ump_endpoint_configuration& config) noexcept;

  /// Create an endpoint with a specific backend configuration
  /// @throws std::system_error if the backend cannot be initialized
  explicit ump_endpoint(
      const remote_ump_endpoint_configuration& config,
      const endpoint_api_configuration& api_config);

  ump_endpoint(const ump_endpoint&) = delete;
  ump_endpoint(ump_endpoint&& other) noexcept;
  ump_endpoint& operator=(const ump_endpoint&) = delete;
  ump_endpoint& operator=(ump_endpoint&& other) noexcept;
  ~ump_endpoint();

  /// Get the current API being used
  [[nodiscard]] libremidi::API get_current_api() const noexcept;

  /*
  /// Open a connection to a UMP endpoint
  /// @param endpoint_info Information about the endpoint to connect to
  /// @param local_name Optional name for this connection (for virtual ports)
  /// @return Error code, or empty error on success
  stdx::error open(
      const ump_endpoint_info& endpoint_info,
      std::string_view local_name = "libremidi endpoint");

  /// Create and open a virtual endpoint
  /// @param port_name Name of the virtual endpoint
  /// @return Error code, or empty error on success
  stdx::error open_virtual(std::string_view port_name = "libremidi virtual endpoint");

  /// Close the connection
  stdx::error close();

  /// Check if the endpoint is currently open
  [[nodiscard]] bool is_open() const noexcept;

  /// Check if connected to another endpoint (not just open)
  [[nodiscard]] bool is_connected() const noexcept;

  /// Get information about the connected endpoint (if any)
  [[nodiscard]] std::optional<ump_endpoint_info> get_endpoint_info() const noexcept;

  /// Get current function blocks for this endpoint
  [[nodiscard]] std::vector<function_block_info> get_function_blocks() const noexcept;

  /// Get a specific function block
  [[nodiscard]] std::optional<function_block_info>
  get_function_block(uint8_t block_id) const noexcept;

  /// Get the currently active protocol
  [[nodiscard]] midi_protocol get_active_protocol() const noexcept;

  /// Request a protocol change (if supported by endpoint)
  /// @param protocol Desired protocol
  /// @return Error code, or empty error if request was sent
  /// @note Protocol change is asynchronous; monitor on_endpoint_updated
  stdx::error request_protocol(midi_protocol protocol) const;

  /// Check if JR Timestamps are currently enabled
  [[nodiscard]] bool jr_timestamps_enabled() const noexcept;

  /// Send a MIDI-CI discovery message
  /// @return Error code, or empty error on success
  stdx::error send_ci_discovery() const;

  /// Check if the endpoint supports MIDI-CI
  [[nodiscard]] bool supports_midi_ci() const noexcept;

  /// Set the local port name (if supported by backend)
  stdx::error set_port_name(std::string_view name);
*/
private:
  std::unique_ptr<ump_endpoint_api> m_impl;
};

class local_endpoint;
struct connection
{
  ump_endpoint* source{};
  local_endpoint* sink{};
  connection_configuration conf;
  virtual void stop();

  /// Send a single UMP message
  /// @param message The UMP message to send
  /// @return Error code, or empty error on success
  stdx::error send(const ump& message) const;

  /// Send a UMP message from raw words
  stdx::error send(std::span<const uint32_t> words) const;

  /// Send a single-word UMP (utility, system, MIDI 1.0 channel voice)
  stdx::error send(uint32_t word0) const;

  /// Send a two-word UMP (MIDI 2.0 channel voice, sysex7)
  stdx::error send(uint32_t word0, uint32_t word1) const;

  /// Send a three-word UMP (reserved for future use)
  stdx::error send(uint32_t word0, uint32_t word1, uint32_t word2) const;

  /// Send a four-word UMP (sysex8, flex data, stream)
  stdx::error send(uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3) const;

  /// Send multiple UMP messages in a batch
  /// @param messages Array of UMP messages
  /// @return Error code, or empty error on success
  stdx::error send(std::span<const ump> messages) const;

  /// Send raw UMP words (must be properly formatted)
  /// @param words Raw UMP word data
  /// @param count Number of 32-bit words
  /// @return Error code, or empty error on success
  stdx::error send_raw(const uint32_t* words, size_t count) const;

  /// Schedule a UMP message for future delivery
  /// @param timestamp Absolute timestamp in the endpoint's time domain
  /// @param message The UMP message to send
  /// @return Error code, or empty error on success
  /// @note Not all backends support scheduling
  stdx::error schedule(int64_t timestamp, const ump& message) const;

  /// Get the current time in the endpoint's time domain
  [[nodiscard]] int64_t current_time() const noexcept;

  /// Send a UMP message to a specific group
  /// @param group Target group (0-15)
  /// @param message The UMP message (group field will be overwritten)
  /// @return Error code, or empty error on success
  stdx::error send_to_group(uint8_t group, ump message) const;

  /// Send a UMP message to a specific function block
  /// @param block_id Target function block ID
  /// @param message The UMP message
  /// @return Error code, or empty error on success
  /// @note Message is sent to the first group of the function block
  stdx::error send_to_block(uint8_t block_id, ump message) const;

  std::unique_ptr<ump_connection_api> m_impl;
};

// ALSA: the seq object
// CoreMidi: Client
// WinMIDI: session
class LIBREMIDI_EXPORT local_endpoint
{
  explicit local_endpoint(const local_endpoint_configuration& config) noexcept;
  // stdx::error send_ump(const ump_endpoint& to, const ump& message) const;
  // connection receive(const ump_endpoint& from, std::function<void(ump)> func);
  connection connect(ump_endpoint& target, connection_configuration conf);

  // will update if already in
  void add_function_block(function_block_info);
  void remove_function_block(function_block_info);

  std::array<function_block_info, 255> blocks;
};

//=============================================================================
// Utility Functions
//=============================================================================

/// Check if a UMP backend is available on this system
[[nodiscard]] LIBREMIDI_EXPORT bool ump_backend_available() noexcept;

/// Get the default UMP backend for this platform
[[nodiscard]] LIBREMIDI_EXPORT libremidi::API default_ump_api() noexcept;

/// Get all available UMP backends
[[nodiscard]] LIBREMIDI_EXPORT std::vector<libremidi::API> available_ump_apis() noexcept;

} // namespace libremidi
