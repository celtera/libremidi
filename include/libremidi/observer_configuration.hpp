#pragma once
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>

#include <array>
#include <compare>
#include <string>
#include <variant>

namespace libremidi
{
using client_handle = std::uint64_t;
using port_handle = std::uint64_t;

struct uuid
{
  std::array<uint8_t, 16> bytes;

  bool operator==(const uuid& other) const noexcept = default;
  std::strong_ordering operator<=>(const uuid& other) const noexcept = default;
};
using container_identifier = std::variant<std::monostate, uuid, std::string, std::uint64_t>;
using device_identifier = std::variant<std::monostate, std::string, std::uint64_t>;

struct LIBREMIDI_EXPORT port_information
{
  enum port_type : uint8_t
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

  /// Handle to the API client object if the API provides one
  // ALSA Raw: unused
  // ALSA Seq: snd_seq_t*
  // CoreMIDI: MidiClientRef
  // WebMIDI: unused
  // JACK: jack_client_t*
  // PipeWire: unused // FIXME: pw_context? pw_main_loop?
  // WinMM: unused
  // WinUWP: unused
  client_handle client = static_cast<client_handle>(-1);

  /// Container identifier if the API provides one
  // WinMIDI: ContainerID GUID (bit_cast to a winapi or winrt::GUID ;
  //        this is not the string but the binary representation).
  // ALSA: device id (std::string), e.g. ID_PATH as returned by udev: "pci-0000:00:14.0-usb-0:12:1.0"
  // CoreMIDI: USBLocationID (int32_t)
  container_identifier container = std::monostate{};

  /// Device identifier if the API provides one
  // WinMM: { uint16_t manufacturer_id, uint16_t product_id; }
  // WinMIDI: EndpointDeviceId (std::string), e.g. "\\?\swd#midisrv#midiu_ksa..."
  // ALSA: sysfs path (std::string), e.g. "/sys/devices/pci0000:00/0000:00:02.2/0000:02:00.0/sound/card0/controlC0"
  // CoreMIDI: USBVendorProduct (int32_t)
  device_identifier device = std::monostate{};

  /// Handle to the port identifier if the API provides one
  // ALSA Raw: bit_cast to struct { uint16_t card, device, sub, padding; }.
  // ALSA Seq: bit_cast to struct { uint32_t client, port; }
  // CoreMIDI: MidiObjectRef's kMIDIPropertyUniqueID (uint32_t)
  // WebMIDI: unused
  // JACK: jack_port_id_t
  // PipeWire: port.id
  // WinMIDI: uint64_t terminal_block_number; (MidiGroupTerminalBlock::Number(), index is 1-based)
  // WinMM: port index between 0 and midi{In,Out}GetNumDevs()
  // WinUWP: unused
  port_handle port = static_cast<port_handle>(-1);

  /// User-readable information
  // CoreMIDI: kMIDIPropertyManufacturer
  // WinMIDI: MidiEndpointDeviceInformation::GetTransportSuppliedInfo().ManufacturerName
  // WinMM: unavailable
  std::string manufacturer{};

  // CoreMIDI: kMIDIPropertyModel
  // WinMIDI: MidiEndpointDeviceInformation::Name
  // WinMM: unavailable
  std::string device_name{};

  // CoreMIDI: kMIDIPropertyName
  // WinMIDI: MidiGroupTerminalBlock::Name
  // WinMM: szPname
  std::string port_name{};

  // CoreMIDI: kMIDIPropertyDisplayName
  // Otherwise: the closest to a unique name we can get
  std::string display_name{};

  /// Port type
  // CoreMIDI: available
  // WinMM: unavailable
  // WinMIDI: available
  port_type type = port_type::unknown;

  bool operator==(const port_information& other) const noexcept = default;
  std::strong_ordering operator<=>(const port_information& other) const noexcept = default;
};

struct input_port : port_information
{
  bool operator==(const input_port& other) const noexcept = default;
  std::strong_ordering operator<=>(const input_port& other) const noexcept = default;
};
struct output_port : port_information
{
  bool operator==(const output_port& other) const noexcept = default;
  std::strong_ordering operator<=>(const output_port& other) const noexcept = default;
};

using input_port_callback = std::function<void(const input_port&)>;
using output_port_callback = std::function<void(const output_port&)>;
struct observer_configuration
{
  midi_error_callback on_error{};
  midi_warning_callback on_warning{};

  input_port_callback input_added{};
  input_port_callback input_removed{};
  output_port_callback output_added{};
  output_port_callback output_removed{};

  // Observe hardware ports
  uint32_t track_hardware : 1 = true;

  // Observe software (virtual) ports if the API provides it
  uint32_t track_virtual : 1 = false;

  // Observe any port - some systems have other weird port types than hw / sw, this covers them
  uint32_t track_any : 1 = false;

  // Notify of the existing ports in the observer constructor
  uint32_t notify_in_constructor : 1 = true;

  bool has_callbacks() const noexcept
  {
    return input_added || input_removed || output_added || output_removed;
  }
};
}
