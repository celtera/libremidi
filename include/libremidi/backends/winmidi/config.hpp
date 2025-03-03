#pragma once
#include <libremidi/config.hpp>

#include <string>

namespace winrt::Microsoft::Windows::Devices::Midi2
{
class MidiSession;
class MidiEndpointDeviceInformation;
class MidiEndpointConnection;
}

// TODO allow to share midi session and endpoints
namespace libremidi::winmidi
{

struct input_configuration
{
  std::string client_name = "libremidi input";
  winrt::Microsoft::Windows::Devices::Midi2::MidiSession* context{};
};

struct output_configuration
{
  std::string client_name = "libremidi output";
  winrt::Microsoft::Windows::Devices::Midi2::MidiSession* context{};
};

struct observer_configuration
{
  std::string client_name = "libremidi observer";
};

}
