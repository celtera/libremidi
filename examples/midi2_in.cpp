#include "utils.hpp"

#include <libremidi/backends/backends.hpp>
#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>

int main(int argc, char**)
try
{
  libremidi::observer obs{{}, libremidi::alsa_raw_observer_configuration{}};

  libremidi::midi_in midiin{
      {
          // Set our callback function.
          .on_message
          = [](const libremidi::ump& message) { std::cout << message.bytes[0] << std::endl; },

          // Don't ignore sysex, timing, or active sensing messages.
          .ignore_sysex = false,
          .ignore_timing = false,
          .ignore_sensing = false,
      },
      libremidi::alsa_raw_ump::input_configuration{}};

  auto p = obs.get_input_ports();
  if (p.empty())
  {
    std::cerr << "No device found\n";
    return -1;
  }

  for (const auto& port : p)
  {
    std::cout << port.port_name << " | " << port.display_name << std::endl;
  }
  midiin.open_port(p[0]);
  std::cout << "\nReading MIDI input ... press <enter> to quit.\n";
  char input;
  std::cin.get(input);
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
