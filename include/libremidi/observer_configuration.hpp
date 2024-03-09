#pragma once
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>

#include <compare>
#include <string>

namespace libremidi
{
using client_handle = std::uint64_t;
using port_handle = std::uint64_t;

struct LIBREMIDI_EXPORT port_information
{
  // Handle to the client object:

  // ALSA Raw: unused
  // ALSA Seq: snd_seq_t*
  // CoreMIDI: MidiClientRef
  // WebMIDI: unused
  // JACK: jack_client_t*
  // PipeWire: unused
  // WinMM: unused
  // WinUWP: unused
  client_handle client;

  // ALSA Raw: { uint16_t card, device, sub, padding; }
  // ALSA Seq: { uint32_t client, uint32_t port; }
  // CoreMIDI: MidiObjectRef
  // WebMIDI: unused
  // JACK: jack_port_id_t
  // PipeWire: port.id
  // WinMM: unset, identified by port_name
  // WinUWP: unused
  port_handle port;

  std::string manufacturer;
  std::string device_name;
  std::string port_name;
  std::string display_name;

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

  input_port_callback input_added;
  input_port_callback input_removed;
  output_port_callback output_added;
  output_port_callback output_removed;

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
