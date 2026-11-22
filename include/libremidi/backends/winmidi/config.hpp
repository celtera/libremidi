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
};

struct output_configuration
{
  std::string client_name;
  winrt::Windows::Devices::Midi2::MidiSession* context{};
};

struct observer_configuration
{
  std::string client_name = "libremidi observer";
};

}
