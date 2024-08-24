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

int main(int argc, const char** argv)
{
  using namespace std::literals;
  libremidi::examples::arguments args{argc, argv};

  libremidi::midi_out midiout{{}, libremidi::midi_out_configuration_for(args.api)};
  libremidi::midi_in midiin{
      {
          // Set our callback function.
          .on_message
          = [](const libremidi::message& message) { std::cout << message << std::endl; },

          .ignore_sysex = false,
          .ignore_timing = true,
          .ignore_sensing = true,
      },
      libremidi::midi_in_configuration_for(args.api)};

  if (!args.open_port(midiin))
    return 1;
  if (!args.open_port(midiout))
    return 1;

  midiout.send_message(0xF6);
  std::this_thread::sleep_for(500ms); // pause a little

  // Create a long sysex message of numbered bytes and send it out ... twice.
  std::vector<unsigned char> message;
  for (int n = 0; n < 2; n++)
  {
    message.clear();
    message.push_back(240);
    for (auto i = 0U; i < args.count; i++)
      message.push_back(i % 128);

    message.push_back(247);
    // Note: midiout.send_message(message) should work...
    // but it fails on the CI with libc++-14 (works fine with later versions)
    midiout.send_message(message.data(), message.size());

    std::this_thread::sleep_for(500ms); // pause a little
  }
}
