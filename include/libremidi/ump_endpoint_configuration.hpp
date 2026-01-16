#pragma once
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>
#include <libremidi/input_configuration.hpp>
#include <libremidi/output_configuration.hpp>
#include <libremidi/ump.hpp>
#include <libremidi/ump_endpoint_info.hpp>

#include <bitset>
#include <functional>
#include <string>

namespace libremidi
{
/// Callback for endpoint state changes
using endpoint_state_callback = std::function<void(const ump_endpoint_info&)>;

/// Callback when function blocks change (dynamic blocks)
using function_block_callback = std::function<void(const std::vector<function_block_info>&)>;

/// Filter mode for group-based message filtering
enum class group_filter_mode : uint8_t
{
  all_groups,      ///< Receive/send on all groups
  single_group,    ///< Receive/send on a single group
  group_range,     ///< Receive/send on a range of groups
  function_block   ///< Receive/send on groups belonging to a function block
};

/// Configuration for group filtering
struct group_filter
{
  group_filter_mode mode{group_filter_mode::all_groups};

  /// For single_group mode
  uint8_t group{0};

  /// For group_range mode
  libremidi::group_range range{};

  /// For function_block mode
  uint8_t function_block_id{0};

  /// Create filter for all groups
  static constexpr group_filter all() noexcept
  {
    return {group_filter_mode::all_groups, 0, {}, 0};
  }

  /// Create filter for single group
  static constexpr group_filter single(uint8_t g) noexcept
  {
    return {group_filter_mode::single_group, g, {}, 0};
  }

  /// Create filter for group range
  static constexpr group_filter range_of(uint8_t first, uint8_t count) noexcept
  {
    return {group_filter_mode::group_range, 0, {first, count}, 0};
  }

  /// Create filter for function block
  static constexpr group_filter block(uint8_t fb_id) noexcept
  {
    return {group_filter_mode::function_block, 0, {}, fb_id};
  }
};

/// Protocol preference for endpoint communication
enum class protocol_preference : uint8_t
{
  prefer_midi2,    ///< Use MIDI 2.0 Protocol if available, fall back to MIDI 1.0
  prefer_midi1,    ///< Use MIDI 1.0 Protocol if available
  require_midi2,   ///< Require MIDI 2.0 Protocol, fail if not available
  require_midi1    ///< Require MIDI 1.0 Protocol, fail if not available
};

struct connection_configuration
{
  /// Callback for receiving UMP messages
  ump_callback on_message{};

  /// Callback for receiving raw UMP stream
  raw_ump_callback on_raw_data{};

  /// Filter for incoming messages
  group_filter input_filter{group_filter::all()};

  /// Filter for outgoing messages (applies group remapping if needed)
  group_filter output_filter{group_filter::all()};

  /// Preferred protocol
  protocol_preference protocol{protocol_preference::prefer_midi2};

  /// Enable Jitter Reduction Timestamps (if supported)
  uint32_t enable_jr_timestamps : 1 = false;

  //! Upscale MIDI 1 channel events to MIDI 2 channel events.
  //! Note that this only has an effect on Windows with MIDI Services
  //! as other platforms already do this by default.
  uint32_t midi1_channel_events_to_midi2 : 1 = true;

  /// Ignore System Exclusive messages
  bool ignore_sysex{false};

  /// Ignore MIDI Time Code messages
  bool ignore_timing{false};

  /// Ignore Active Sensing messages
  bool ignore_sensing{true};
};

struct local_endpoint_configuration
{
  /// Client name (visible to other applications)
  std::string name{"libremidi client"};

  /// Callback for errors
  midi_error_callback on_error{};

  /// Callback for warnings
  midi_warning_callback on_warning{};

  // /// Create a virtual endpoint (software endpoint visible to other apps)
  // bool create_virtual{true};

  // necessary? available groups
  std::bitset<32> groups{};
};

/// Configuration for opening a UMP endpoint
struct remote_ump_endpoint_configuration
{
  /// Called when the endpoint is disconnected unexpectedly
  std::function<void()> on_disconnected{};

  /// Called when endpoint information changes
  endpoint_state_callback on_endpoint_updated{};

  /// Called when function blocks change (for dynamic function blocks)
  function_block_callback on_function_blocks_changed{};

  /// Callback for errors
  midi_error_callback on_error{};

  /// Callback for warnings
  midi_warning_callback on_warning{};

  // /// Create a virtual endpoint (software endpoint visible to other apps)
  // bool create_virtual{false};

  // /// For virtual endpoints: direction of the virtual endpoint
  // function_block_direction virtual_direction{function_block_direction::bidirectional};

  // /// Buffer size for message reception (0 = platform default)
  // size_t buffer_size{0};

  // /// Enable automatic endpoint discovery refresh
  // bool auto_refresh_endpoint_info{true};
};

} // namespace libremidi
