#pragma once
#include <libremidi/config.hpp>

#include <string>

#include <libremidi/backends/winmidi/session_fwd.hpp>

NAMESPACE_LIBREMIDI::winmidi
{

struct input_configuration
{
  std::string client_name = "libremidi input";
  winrt::Windows::Devices::Midi2::MidiSession* context{};
};

struct output_configuration
{
  std::string client_name = "libremidi output";
  winrt::Windows::Devices::Midi2::MidiSession* context{};
};

struct observer_configuration
{
  std::string client_name = "libremidi observer";
};

}
