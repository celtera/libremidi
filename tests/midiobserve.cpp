// midiprobe.cpp
//
// Simple program to check MIDI inputs and outputs.
//
// by Gary Scavone, 2003-2012.

#include <cstdlib>
#include <iostream>
#include <map>
#include <libremidi/libremidi.hpp>

#if defined(__APPLE__)
  #include <CoreFoundation/CoreFoundation.h>
#endif

std::ostream& operator<<(std::ostream& s, const libremidi::port_information& rhs)
{
  return s << "[ client: " << rhs.client << ", port: " << rhs.port
           << ", manufacturer: " << rhs.manufacturer << ", devname: " << rhs.device_name
           << ", portname: " << rhs.port_name << ", display: " << rhs.display_name << "]\n";
}
int main()
try
{
  // Create an api map.
  static std::map<libremidi::API, std::string> apiMap{
      {libremidi::API::MACOSX_CORE, "OS-X CoreMidi"},
      {libremidi::API::WINDOWS_MM, "Windows MultiMedia"},
      {libremidi::API::WINDOWS_UWP, "Windows UWP"},
      {libremidi::API::UNIX_JACK, "Jack Client"},
      {libremidi::API::LINUX_ALSA_SEQ, "Linux ALSA (Seq)"},
      {libremidi::API::LINUX_ALSA_RAW, "Linux ALSA (Raw)"},
      {libremidi::API::DUMMY, "Dummy (no driver)"},
  };

  std::vector<std::unique_ptr<libremidi::observer>> observers;
  std::cout << "\nCompiled APIs:\n";
  for (auto api : libremidi::available_apis())
  {
    libremidi::observer_configuration cbs;
    cbs.input_added = [=](const libremidi::port_information& p) {
      std::cerr << apiMap[api] << " : input added " << p << "\n";
    };
    cbs.input_removed = [=](const libremidi::port_information& p) {
      std::cerr << apiMap[api] << " : input removed " << p << "\n";
    };
    cbs.output_added = [=](const libremidi::port_information& p) {
      std::cerr << apiMap[api] << " : output added " << p << "\n";
    };
    cbs.output_removed = [=](const libremidi::port_information& p) {
      std::cerr << apiMap[api] << " : output removed " << p << "\n";
    };
    std::cout << "  " << apiMap[api] << std::endl;
    observers.push_back(std::make_unique<libremidi::observer>(api, cbs));
  }

  std::cerr << "... waiting for hotplug events ...\n";

#if defined(__APPLE__)
  // On macOS, observation can *only* be done in the main thread
  // with an active CFRunLoop.
  CFRunLoopRun();
#else
  getchar();
#endif
  return 0;
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
