#pragma once
#include <libremidi/api.hpp>
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>
#include <libremidi/types.hpp>
#include <libremidi/ump.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace libremidi
{

/// Identifies a range of MIDI 2.0 groups (0-15)
struct group_range
{
  uint8_t first_group{0};
  uint8_t num_groups{1};

  constexpr bool contains(uint8_t group) const noexcept
  {
    return group >= first_group && group < first_group + num_groups;
  }

  constexpr uint8_t last_group() const noexcept
  {
    return first_group + num_groups - 1;
  }

  bool operator==(const group_range&) const noexcept = default;
};

/// Direction of a Function Block
enum class function_block_direction : uint8_t
{
  input = 0x01,       ///< Receives MIDI messages
  output = 0x02,      ///< Sends MIDI messages
  bidirectional = 0x03 ///< Both sends and receives
};

/// UI hint for how a Function Block is primarily used
enum class function_block_ui_hint : uint8_t
{
  unknown = 0x00,
  receiver = 0x01,   ///< Primarily a destination for MIDI messages
  sender = 0x02,     ///< Primarily a source of MIDI messages
  both = 0x03        ///< Both sender and receiver
};

/// Information about a MIDI 2.0 Function Block
struct function_block_info
{
  /// Block identifier (0-31 typically, though spec allows 0-255)
  uint8_t block_id{0};

  /// MIDI-CI version supported (0 = no MIDI-CI)
  uint8_t midi_ci_version{0};

  /// Maximum number of simultaneous SysEx8 streams
  uint8_t max_sysex8_streams{0};

  /// Whether this block is currently active
  bool active{true};

  /// Whether this is a MIDI 1.0 port (legacy device)
  bool is_midi1{false};

  /// Whether this is a low-speed (31.25 kbaud) MIDI 1.0 port
  bool is_low_speed{false};

  /// Direction of this function block
  function_block_direction direction{function_block_direction::bidirectional};

  /// UI hint for primary use
  function_block_ui_hint ui_hint{function_block_ui_hint::unknown};

  /// Groups spanned by this function block
  group_range groups{};

  /// Human-readable name
  std::string name{};

  /// Check if this block can receive MIDI messages
  [[nodiscard]] constexpr bool can_receive() const noexcept
  {
    return direction == function_block_direction::input
           || direction == function_block_direction::bidirectional;
  }

  /// Check if this block can send MIDI messages
  [[nodiscard]] constexpr bool can_send() const noexcept
  {
    return direction == function_block_direction::output
           || direction == function_block_direction::bidirectional;
  }

  /// Check if this block supports MIDI-CI
  [[nodiscard]] constexpr bool supports_midi_ci() const noexcept
  {
    return midi_ci_version > 0;
  }

  bool operator==(const function_block_info&) const noexcept = default;
};

/// MIDI device identity (from Device Identity Notification)
struct midi_device_identity
{
  /// Manufacturer ID (1 or 3 bytes, stored as 24-bit value)
  uint32_t manufacturer_id{0};

  /// Device family ID
  uint16_t family_id{0};

  /// Device model ID
  uint16_t model_id{0};

  /// Software revision (4 bytes)
  std::array<uint8_t, 4> software_revision{};

  bool operator==(const midi_device_identity&) const noexcept = default;
};

/// Jitter Reduction Timestamp capabilities
struct jr_timestamp_caps
{
  bool can_receive{false};
  bool can_transmit{false};

  bool operator==(const jr_timestamp_caps&) const noexcept = default;
};

/// Transport type for the endpoint
enum class endpoint_transport_type : uint8_t
{
  unknown = 0,
  usb = 1,
  bluetooth = 2,
  network = 3,
  virtual_port = 4,
  pci = 5,
  other = 255
};

/// Complete information about a UMP Endpoint
struct LIBREMIDI_EXPORT ump_endpoint_info
{
  /// Which API is this port for. port_information objects are in general
  /// not useable for different APIs than the API of the observer that created them.
  libremidi::API api{};

  /// Handle to the API client object if the API provides one
  // ALSA Raw: unused
  // ALSA Seq: snd_seq_t*
  // CoreMIDI: MidiClientRef
  // WebMIDI: unused
  // JACK: jack_client_t*
  // PipeWire: unused // FIXME: pw_context? pw_main_loop?
  // WinMIDI: TODO
  // WinMM: unused
  // WinUWP: unused
  client_handle client = static_cast<client_handle>(-1);

  /// Container identifier if the API provides one
  // ALSA: device id (std::string), e.g. ID_PATH as returned by udev: "pci-0000:00:14.0-usb-0:12:1.0"
  // CoreMIDI: USBLocationID (int32_t)
  // WinMIDI: ContainerID GUID (bit_cast to a winapi or winrt::GUID ;
  //        this is not the string but the binary representation).
  container_identifier container{};

  /// Device identifier if the API provides one
  // WinMM: MIDI{IN,OUT}CAPS mId / pId { uint16_t manufacturer_id, uint16_t product_id; }
  // WinMIDI: EndpointDeviceId (std::string), e.g. "\\?\swd#midisrv#midiu_ksa..."
  // ALSA: sysfs path (std::string), e.g. "/sys/devices/pci0000:00/0000:00:02.2/0000:02:00.0/sound/card0/controlC0"
  // CoreMIDI: USBVendorProduct (int32_t)
  device_identifier device = std::monostate{};

  /// Platform-specific endpoint identifier (for opening)
  // ALSA Seq: client index
  // CoreMIDI: MIDIObjectRef
  // Windows: EndpointDeviceId string
  endpoint_identifier endpoint_id{};

  /// Product Instance ID (usually serial number)
  // ALSA Raw
  std::string product_instance_id{};

  /// Endpoint name (from Endpoint Name Notification)
  std::string name{};

  /// Manufacturer name (if available from transport)
  std::string manufacturer{};

  /// Display name for UI
  std::string display_name{};

  /// UMP format version supported
  ump_version version{};

  /// Protocols supported by this endpoint
  midi_protocol supported_protocols{midi_protocol::both};

  /// Currently active protocol
  midi_protocol active_protocol{midi_protocol::midi1};

  /// Jitter Reduction Timestamp capabilities
  jr_timestamp_caps jr_timestamps{};

  /// Device identity information
  std::optional<midi_device_identity> device_identity{};

  /// Transport type
  endpoint_transport_type transport{endpoint_transport_type::unknown};

  /// Whether function blocks are static (won't change at runtime)
  bool static_function_blocks{false};

  /// Function blocks in this endpoint
  std::vector<function_block_info> function_blocks{};

  /// Check if this endpoint supports MIDI 2.0 Protocol
  [[nodiscard]] bool supports_midi2() const noexcept
  {
    return supported_protocols == midi_protocol::midi2
           || supported_protocols == midi_protocol::both;
  }

  /// Check if this endpoint supports MIDI 1.0 Protocol
  [[nodiscard]] bool supports_midi1() const noexcept
  {
    return supported_protocols == midi_protocol::midi1
           || supported_protocols == midi_protocol::both;
  }

  /// Get function blocks that can receive messages
  /// Pass e.g. a lambda such as
  /// [] (const libremidi::function_block_info& fblock) { }
  void input_blocks(auto callback) const noexcept
  {
    for (const auto& fb : function_blocks)
      if (fb.can_receive() && fb.active)
        callback(fb);
  }

  /// Get function blocks that can send messages
  void output_blocks(auto callback) const noexcept
  {
    for (const auto& fb : function_blocks)
      if (fb.can_send() && fb.active)
        callback(fb);
  }

  /// Get bidirectional function blocks
  void bidirectional_blocks(auto callback) const noexcept
  {
    for (const auto& fb : function_blocks)
      if (fb.direction == function_block_direction::bidirectional && fb.active)
        callback(fb);
  }

  /// Find function block by ID
  [[nodiscard]] const function_block_info* find_block(uint8_t block_id) const noexcept
  {
    for (const auto& fb : function_blocks)
      if (fb.block_id == block_id)
        return &fb;
    return nullptr;
  }

  /// Find function block containing a specific group
  [[nodiscard]] const function_block_info* find_block_for_group(uint8_t group) const noexcept
  {
    for (const auto& fb : function_blocks)
      if (fb.groups.contains(group) && fb.active)
        return &fb;
    return nullptr;
  }

  /// Check if the endpoint has any input capability
  [[nodiscard]] bool has_input() const noexcept
  {
    for (const auto& fb : function_blocks)
      if (fb.can_receive() && fb.active)
        return true;
    return false;
  }

  /// Check if the endpoint has any output capability
  [[nodiscard]] bool has_output() const noexcept
  {
    for (const auto& fb : function_blocks)
      if (fb.can_send() && fb.active)
        return true;
    return false;
  }

  /// Check if the endpoint is fully bidirectional
  [[nodiscard]] bool is_bidirectional() const noexcept
  {
    return has_input() && has_output();
  }

  bool operator==(const ump_endpoint_info&) const noexcept = default;
};

} // namespace libremidi
