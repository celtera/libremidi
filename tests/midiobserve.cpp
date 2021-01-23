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
  static std::map<remidi::API, std::string> apiMap{
      {remidi::API::MACOSX_CORE, "OS-X CoreMidi"}, {remidi::API::WINDOWS_MM, "Windows MultiMedia"},
      {remidi::API::WINDOWS_UWP, "Windows UWP"},   {remidi::API::UNIX_JACK, "Jack Client"},
      {remidi::API::LINUX_ALSA, "Linux ALSA"},     {remidi::API::DUMMY, "Dummy (no driver)"},
  };

  std::vector<std::unique_ptr<remidi::observer>> observers;
  std::cout << "\nCompiled APIs:\n";
  for (auto api : remidi::available_apis())
  {
    remidi::observer::callbacks cbs;
    cbs.input_added = [=](int i, std::string n) {
      std::cerr << apiMap[api] << " : input added " << i << " => " << n << "\n";
    };
    cbs.input_removed = [=](int i, std::string n) {
      std::cerr << apiMap[api] << " : input removed " << i << " => " << n << "\n";
    };
    cbs.output_added = [=](int i, std::string n) {
      std::cerr << apiMap[api] << " : output added " << i << " => " << n << "\n";
    };
    cbs.output_removed = [=](int i, std::string n) {
      std::cerr << apiMap[api] << " : output removed " << i << " => " << n << "\n";
    };
    std::cout << "  " << apiMap[api] << std::endl;
    observers.push_back(std::make_unique<remidi::observer>(api, cbs));
  }

  getchar();
  return 0;
}
catch (const remidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
