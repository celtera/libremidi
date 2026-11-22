#pragma once
#include <libremidi/config.hpp>

#include <string>

#include <libremidi/backends/winmidi/session_fwd.hpp>

NAMESPACE_LIBREMIDI::winmidi
{

struct input_configuration
{
  std::string client_name; // Empty: share the host application's default session.
  winrt::Windows::Devices::Midi2::MidiSession* context{};

  //! Identifies the endpoint that open_virtual_port creates. The service derives
  //! the endpoint device id from it: keep it identical across runs for other
  //! applications to find the same endpoint again.
  //! 1 to 32 characters among [A-Za-z0-9_-], compared without case. It must not
  //! be used by another live virtual device, in this process or another one:
  //! an input and an output need different identifiers.
  //! Empty: a new random identifier for every open_virtual_port.
  std::string virtual_endpoint_id;
};

struct output_configuration
{
  std::string client_name;
  winrt::Windows::Devices::Midi2::MidiSession* context{};

  //! See input_configuration::virtual_endpoint_id.
  std::string virtual_endpoint_id;
};

struct observer_configuration
{
  std::string client_name = "libremidi observer";
};

}
