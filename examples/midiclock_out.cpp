//*****************************************//
//  midiclock.cpp
//
//  Simple program to test MIDI clock sync.  Run midiclock_in in one
//  console and midiclock_out in the other, make sure to choose
//  options that connect the clocks between programs on your platform.
//
//  (C)2016 Refer to README.md in this archive for copyright.
//
//*****************************************//

#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int, const char* argv[])
try
{
  using namespace std::literals;
  libremidi::midi_out midiout;

  // Call function to select port.
  if (chooseMidiPort(midiout) == false)
    return 0;

  // Period in ms = 100 BPM
  // 100*24 ticks / 1 minute, so (60*1000) / (100*24) = 25 ms / tick
  int sleep_ms = 25;
  std::cout << "Generating clock at " << (60.0 / 24.0 / sleep_ms * 1000.0) << " BPM." << std::endl;

  // Send out a series of MIDI clock messages.
  // MIDI start
  midiout.send_message(0xFA);
  std::cout << "MIDI start" << std::endl;

  for (int j = 0; j < 8; j++)
  {
    if (j > 0)
    {
      // MIDI continue
      midiout.send_message(0xFB);
      std::cout << "MIDI continue" << std::endl;
    }

    for (int k = 0; k < 96; k++)
    {
      // MIDI clock
      midiout.send_message(0xF8);
      if (k % 24 == 0)
        std::cout << "MIDI clock (one beat)" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    // MIDI stop
    midiout.send_message(0xFC);
    std::cout << "MIDI stop" << std::endl;
    std::this_thread::sleep_for(500ms);
  }

  // MIDI stop
  midiout.send_message(0xFC);
  std::cout << "MIDI stop" << std::endl;

  std::this_thread::sleep_for(500ms);

  std::cout << "Done!" << std::endl;

  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  exit(EXIT_FAILURE);
}
