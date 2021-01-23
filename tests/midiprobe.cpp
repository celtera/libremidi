// midiprobe.cpp
//
// Simple program to check MIDI inputs and outputs.
//
// by Gary Scavone, 2003-2012.

#include <cstdlib>
#include <iostream>
#include <map>
#include <remidi/remidi.hpp>

int main()

try
{
  // Create an api map.
  std::map<remidi::API, std::string> apiMap{
      {remidi::API::MACOSX_CORE, "OS-X CoreMidi"}, {remidi::API::WINDOWS_MM, "Windows MultiMedia"},
      {remidi::API::UNIX_JACK, "Jack Client"},     {remidi::API::LINUX_ALSA, "Linux ALSA"},
      {remidi::API::DUMMY, "Dummy (no driver)"},
  };

  auto apis = remidi::available_apis();

  std::cout << "\nCompiled APIs:\n";
  for (auto& api : remidi::available_apis())
  {
    std::cout << "  " << apiMap[api] << std::endl;
  }

  {
    remidi::midi_in midiin;
    std::cout << "\nCurrent input API: " << apiMap[midiin.get_current_api()] << std::endl;

    // Check inputs.
    auto nPorts = midiin.get_port_count();
    std::cout << "\nThere are " << nPorts << " MIDI input sources available.\n";

    for (unsigned i = 0; i < nPorts; i++)
    {
      std::string portName = midiin.get_port_name(i);
      std::cout << "  Input Port #" << i + 1 << ": " << portName << '\n';
    }
  }

  {
    remidi::midi_out midiout;
    std::cout << "\nCurrent output API: " << apiMap[midiout.get_current_api()] << std::endl;

    // Check outputs.
    auto nPorts = midiout.get_port_count();
    std::cout << "\nThere are " << nPorts << " MIDI output ports available.\n";

    for (unsigned i = 0; i < nPorts; i++)
    {
      std::string portName = midiout.get_port_name(i);
      std::cout << "  Output Port #" << i + 1 << ": " << portName << std::endl;
    }
    std::cout << std::endl;
  }
  return 0;
}
catch (const remidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
