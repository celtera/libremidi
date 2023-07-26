// midiprobe.cpp
//
// Simple program to check MIDI inputs and outputs.
//
// by Gary Scavone, 2003-2012.

#include <cstdlib>
#include <iostream>
#include <map>
#include <libremidi/libremidi.hpp>

#include <winrt/Windows.Foundation.h>
int main()

try
{
  // Create an api map.
  std::map<libremidi::API, std::string> apiMap{
      {libremidi::API::MACOSX_CORE, "OS-X CoreMidi"},  {libremidi::API::WINDOWS_MM, "Windows MultiMedia"}, 
      {libremidi::API::WINDOWS_UWP, "Windows UWP"},
      {libremidi::API::UNIX_JACK, "Jack Client"},      {libremidi::API::LINUX_ALSA_SEQ, "Linux ALSA (sequencer)"},
      {libremidi::API::LINUX_ALSA_RAW, "Linux ALSA (raw)"},
      {libremidi::API::DUMMY, "Dummy (no driver)"},
  };

  for (auto& api : libremidi::available_apis())
  {
    std::cout << "\nCurrent API: " << apiMap[api] << std::endl;
  
    // On Windows 10, apparently the MIDI devices aren't exactly available as soon as the app open...
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
    libremidi::observer midi{api, {}};
    {
      // Check inputs.
      auto ports = midi.get_input_ports();
      std::cout << "\nThere are " << ports.size() << " MIDI input sources available.\n";
  
      for (unsigned i = 0; i < ports.size(); i++)
      {
        std::cout << "Input: " << ports[i].display_name << '\n';
      }
    }
  
    {
      // Check outputs.
      auto ports = midi.get_output_ports();
      std::cout << "\nThere are " << ports.size() << " MIDI output sinks available.\n";
  
      for (unsigned i = 0; i < ports.size(); i++)
      {
        std::cout << "Output: " << ports[i].display_name << '\n';
      }
    }
  }
  return 0;
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
