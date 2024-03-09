#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>

int main()
try
{
  libremidi::observer obs{{}, libremidi::midi2::observer_default_configuration()};
  libremidi::midi_out midiout{{}, libremidi::midi2::out_default_configuration()};

  libremidi::midi_in midiin{
      {
          // Set our callback function.
          .on_message
          = [&](const libremidi::ump& message) {
    std::cout << message << std::endl;
    if (midiout.is_port_connected())
      midiout.send_ump(message);
          }
      },
      libremidi::midi2::in_default_configuration()};

  auto pi = obs.get_input_ports();
  if (pi.empty())
  {
    std::cerr << "No MIDI 2 device found\n";
    return -1;
  }

  for (const auto& port : pi)
  {
    std::cout << "In: " << port.port_name << " | " << port.display_name << std::endl;
  }

  auto po = obs.get_output_ports();
  for (const auto& port : po)
  {
    std::cout << "Out: " << port.port_name << " | " << port.display_name << std::endl;
  }
  midiin.open_port(pi[0]);

  if(!po.empty())
    midiout.open_port(po[0]);
  std::cout << "\nReading MIDI input ... press <enter> to quit.\n";

  char input;
  std::cin.get(input);
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
