#pragma once
#include <libremidi/config.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <string>

NAMESPACE_LIBREMIDI
{
/// MIDI protocol supported by an endpoint or function block
enum class midi_protocol : uint8_t
{
  midi1 = (1 << 1),
  midi2 = (1 << 2),
  both = midi1 | midi2
};

/// How a MIDI 1 stream translated to MIDI 2 treats the controllers that
/// only mean something across several messages: bank select (CC 0, 32), data
/// entry (CC 6, 38) and parameter selection (CC 98 to 101).
enum class controller_translation : uint8_t
{
  /// Every message is translated on its own: each of these controllers gives
  /// the MIDI 2 control change of the same index, a program change carries no
  /// bank. Translated back, the stream is the one that came in, whatever a
  /// device uses these controllers for.
  raw,

  /// The default translation of the UMP specification (appendix D.3.3),
  /// channel by channel: a data entry on a selected parameter gives a MIDI 2
  /// registered or assignable controller message, on CC 6 and again on CC 38,
  /// and a bank select is carried by the program change that follows it. The
  /// controllers themselves give nothing.
  semantic
};

enum transport_type : uint8_t
{
  unknown = 0,

  software = (1 << 1),
  loopback = (1 << 2),

  hardware = (1 << 3),
  usb = (1 << 4),
  bluetooth = (1 << 5),
  pci = (1 << 6),

  network = (1 << 7),
};

/// UMP version information
struct ump_version
{
  uint8_t major{1};
  uint8_t minor{1};

  bool operator==(const ump_version&) const noexcept = default;
  std::strong_ordering operator<=>(const ump_version&) const noexcept = default;
};

using client_handle = std::uint64_t;
using port_handle = std::uint64_t;

struct uuid
{
  std::array<uint8_t, 16> bytes;

  bool operator==(const uuid& other) const noexcept = default;
  std::strong_ordering operator<=>(const uuid& other) const noexcept = default;
};

struct usb_device_identifier
{
  uint16_t vendor_id;
  uint16_t product_id;

  bool operator==(const usb_device_identifier& other) const noexcept = default;
  std::strong_ordering operator<=>(const usb_device_identifier& other) const noexcept = default;
};

using container_identifier = libremidi_variant_alias::variant<
    libremidi_variant_alias::monostate, uuid, std::string, std::uint64_t>;
using device_identifier = libremidi_variant_alias::variant<
    libremidi_variant_alias::monostate, std::string, std::uint64_t, usb_device_identifier>;
using endpoint_identifier = libremidi_variant_alias::variant<
    libremidi_variant_alias::monostate, std::string, std::uint64_t>;
}
