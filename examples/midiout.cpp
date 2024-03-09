//*****************************************//
//  midiout.cpp
//  by Gary Scavone, 2003-2004.
//
//  Simple program to test MIDI output.
//
//*****************************************//

#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <array>
#include <chrono>
#include <thread>

int main(void)
try
{
  using namespace std::literals;
  libremidi::midi_out midiout;

  // Call function to select port.
  if (chooseMidiPort(midiout) == false)
    return 0;

  // Send out a series of MIDI messages.

  // Program change: 192, 5
  midiout.send_message(192, 5);

  std::this_thread::sleep_for(500ms);

  midiout.send_message(0xF1, 60);

  // Control Change: 176, 7, 100 (volume)
  midiout.send_message(176, 7, 100);

  // Note On: 144, 64, 90
  midiout.send_message(144, 64, 90);

  std::this_thread::sleep_for(500ms);

  // Note Off: 128, 64, 40
  midiout.send_message(128, 64, 40);

  std::this_thread::sleep_for(500ms);

  // Control Change: 176, 7, 40
  midiout.send_message(176, 7, 40);

  std::this_thread::sleep_for(500ms);

  // Sysex: 240, 67, 4, 3, 2, 247
  midiout.send_message(std::to_array<unsigned char>({240, 67, 4, 3, 2, 247}));

  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  exit(EXIT_FAILURE);
}
