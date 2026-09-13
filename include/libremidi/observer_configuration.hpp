#pragma once
#include <libremidi/port_information.hpp>

NAMESPACE_LIBREMIDI
{
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

  // Observe network ports if the API provides it
  uint32_t track_network : 1 = true;

  // Observe any port - some systems have other weird port types than hw / sw, this covers them
  uint32_t track_any : 1 = false;

  // Notify of the existing ports in the observer constructor
  uint32_t notify_in_constructor : 1 = true;

  bool has_callbacks() const noexcept
  {
    return input_added || input_removed || output_added || output_removed;
  }

  /**
   * @brief Does this observer want a port with the given transport?
   *
   * The one place the track_* flags are interpreted, so that every backend
   * answers this the same way. transport_type is a bitmask - a USB interface is
   * hardware|usb - and each flag matches a group of bits.
   *
   * `unknown` belongs to no group and is admitted only by track_any: a backend
   * that has classified a port as none of hardware, software or network is
   * saying something, and a backend that cannot classify at all should name the
   * transport its ports really have rather than leave it to this rule.
   */
  bool accepts(transport_type t) const noexcept
  {
    if (track_any)
      return true;

    constexpr auto physical
        = transport_type::hardware | transport_type::usb | transport_type::bluetooth
          | transport_type::pci;
    constexpr auto local = transport_type::software | transport_type::loopback;

    if (track_hardware && (t & physical))
      return true;
    if (track_virtual && (t & local))
      return true;
    if (track_network && (t & transport_type::network))
      return true;

    return false;
  }
};
}
