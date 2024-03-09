//*****************************************//
//  cmidiin.cpp
//  by Gary Scavone, 2003-2004.
//
//  Simple program to test MIDI input and
//  use of a user callback function.
//
//*****************************************//

#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>

[[noreturn]] void usage()
{
  // Error function in case of incorrect command-line
  // argument specifications.
  std::cout << "\nuseage: cmidiin <port>\n";
  std::cout << "    where port = the device to use (default = 0).\n\n";
  exit(0);
}

int main(int argc, char**)
try
{
  libremidi::midi_in midiin{{
      // Set our callback function.
      .on_message = [](const libremidi::message& message) { std::cout << message << std::endl; },

      // Don't ignore sysex, timing, or active sensing messages.
      .ignore_sysex = false,
      .ignore_timing = false,
      .ignore_sensing = false,
  }};

  // Minimal command-line check.
  if (argc > 2)
    usage();

  //// Call function to select port.
  if (chooseMidiPort(midiin) == false)
    return 0;

  std::cout << "\nReading MIDI input ... press <enter> to quit.\n";
  int c;
  while ((c = getchar()) != '\n' && c != EOF)
    ;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
