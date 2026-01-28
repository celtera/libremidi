#pragma once
#include <libremidi/port_information.hpp>

NAMESPACE_LIBREMIDI
{
using input_port_callback = std::function<void(const input_port&)>;
using output_port_callback = std::function<void(const output_port&)>;
using endpoint_callback = std::function<void(const ump_endpoint_info&)>;
struct observer_configuration
{
  midi_error_callback on_error{};
  midi_warning_callback on_warning{};

  input_port_callback input_added{};
  input_port_callback input_removed{};
  output_port_callback output_added{};
  output_port_callback output_removed{};

  endpoint_callback endpoint_added{};
  endpoint_callback endpoint_removed{};
  endpoint_callback endpoint_updated{};

  // Observe hardware ports
  uint32_t track_hardware : 1 = true;

  // Observe software (virtual) ports if the API provides it
  uint32_t track_virtual : 1 = false;

  // Observe network ports if the API provides it
  uint32_t track_network : 1 = false;

  // Observe any port - some systems have other weird port types than hw / sw, this covers them
  uint32_t track_any : 1 = false;

  // Notify of the existing ports in the observer constructor
  uint32_t notify_in_constructor : 1 = true;

  // Only include endpoints that support MIDI 1 Protocol
  uint32_t require_midi1 : 1 = false;

  // Only include endpoints that support MIDI 2 Protocol
  uint32_t require_midi2 : 1 = false;

  // Only include endpoints with input capability
  uint32_t require_input : 1 = false;

  // Only include endpoints with output capability
  uint32_t require_output : 1 = false;

  // Only include bidirectional endpoints
  uint32_t require_bidirectional : 1 = false;

  bool has_callbacks() const noexcept
  {
    return input_added || input_removed || output_added || output_removed || endpoint_added
           || endpoint_removed || endpoint_updated;
    ;
  }
};
}
