#include "utils.hpp"

#include <libremidi/backends/backends.hpp>
#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>

int main(int argc, char**)
try
{
#if __has_include(<alsa/ump.h>)
  libremidi::observer obs{{}, libremidi::alsa_raw_observer_configuration{}};

  libremidi::midi_out midiout{{}, libremidi::alsa_raw_ump::output_configuration{}};

  auto p = obs.get_output_ports();
  if (p.empty())
  {
    std::cerr << "No device found\n";
    return -1;
  }

  for (const auto& port : p)
  {
    std::cout << port.port_name << " | " << port.display_name << std::endl;
  }
  midiout.open_port(p[0]);
  std::cout << "\nReading MIDI input ... press <enter> to quit.\n";
  char input;
  std::cin.get(input);
#endif
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
