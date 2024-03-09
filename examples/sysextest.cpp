//*****************************************//
//  sysextest.cpp
//  by Gary Scavone, 2003-2005.
//
//  Simple program to test MIDI sysex sending and receiving.
//
//*****************************************//

#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <typeinfo>

[[noreturn]] void usage()
{
  std::cout << "\nuseage: sysextest N\n";
  std::cout << "    where N = length of sysex message to send / receive.\n\n";
  exit(0);
}

int main(int argc, char* argv[])
try
{
  using namespace std::literals;
  libremidi::midi_out midiout;
  libremidi::midi_in midiin{{
      // Set our callback function.
      .on_message = [](const libremidi::message& message) { std::cout << message << std::endl; },

      .ignore_sysex = false,
      .ignore_timing = true,
      .ignore_sensing = true,
  }};

  // Minimal command-line check.
  if (argc != 2)
    usage();
  auto nBytes = (unsigned int)atoi(argv[1]);

  if (chooseMidiPort(midiin) == false)
    return 0;
  if (chooseMidiPort(midiout) == false)
    return 0;

  midiout.send_message(0xF6);
  std::this_thread::sleep_for(500ms); // pause a little

  // Create a long sysex message of numbered bytes and send it out ... twice.
  std::vector<unsigned char> message;
  for (int n = 0; n < 2; n++)
  {
    message.clear();
    message.push_back(240);
    for (auto i = 0U; i < nBytes; i++)
      message.push_back(i % 128);

    message.push_back(247);
    // Note: midiout.send_message(message) should work...
    // but it fails on the CI with libc++-14 (works fine with later versions)
    midiout.send_message(message.data(), message.size());

    std::this_thread::sleep_for(500ms); // pause a little
  }
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return 0;
}
