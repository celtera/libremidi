// midiprobe.cpp
//
// Simple program to check MIDI inputs and outputs.
//
// by Gary Scavone, 2003-2012.

#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main()
{
  for (auto& api : libremidi::available_apis())
  {
    std::string_view api_name = libremidi::get_api_display_name(api);
    std::cout << "Displaying ports for: " << api_name << std::endl;

    // On Windows 10, apparently the MIDI devices aren't exactly available as soon as the app open...
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    libremidi::observer midi{
        {.track_hardware = true, .track_virtual = true},
        libremidi::observer_configuration_for(api)};
    {
      // Check inputs.
      auto ports = midi.get_input_ports();
      std::cout << ports.size() << " MIDI input sources:\n";
      for (auto& port : ports)
        std::cout << " - " << port << '\n';
    }

    {
      // Check outputs.
      auto ports = midi.get_output_ports();
      std::cout << ports.size() << " MIDI output sinks:\n";
      for (auto& port : ports)
        std::cout << " - " << port << '\n';
    }

    std::cout << "\n";
  }
  return 0;
}
