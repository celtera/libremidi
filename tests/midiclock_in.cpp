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

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

// These functions should be embedded in a try/catch block in case of
// an exception.  It offers the user a choice of MIDI ports to open.
// It returns false if there are no ports available.
template <typename RT>
bool choosePort(RT& libremidi, const std::string& dir)
{
  std::string portName;
  auto port = 0U;
  auto nPorts = libremidi.get_port_count();

  if (nPorts == 0)
  {
    std::cout << "No " << dir << " ports available!" << std::endl;
    return false;
  }
  else if (nPorts == 1)
  {
    std::cout << "\nOpening " << libremidi.get_port_name() << std::endl;
  }
  else
  {
    for (auto i = 0U; i < nPorts; i++)
    {
      portName = libremidi.get_port_name(i);
      std::cout << "  " << dir << " port #" << i << ": " << portName << '\n';
    }

    do
    {
      std::cout << "\nChoose a port number: ";
      std::cin >> port;
    } while (port >= nPorts);
  }

  std::cout << "\n";

  libremidi.open_port(port);

  return true;
}

int main(int, const char* argv[])
try
{
  unsigned int clock_count = 0;
  auto midi_callback = [&](const libremidi::message& message) {
    // Ignore longer messages
    if (message.size() != 1)
      return;

    unsigned int msg = message[0];
    if (msg == 0xFA)
      std::cout << "START received" << std::endl;
    if (msg == 0xFB)
      std::cout << "CONTINUE received" << std::endl;
    if (msg == 0xFC)
      std::cout << "STOP received" << std::endl;
    if (msg == 0xF8)
    {
      if (++clock_count == 24)
      {
        double bpm = 60.0 / 24.0 / message.timestamp;
        std::cout << "One beat, estimated BPM = " << bpm << std::endl;
        clock_count = 0;
      }
    }
    else
      clock_count = 0;
  };

  libremidi::midi_in midiin{libremidi::input_configuration{
      // Setup a callback
      .on_message = midi_callback,

      // Don't ignore sysex, timing, or active sensing messages.
      .ignore_sysex = false,
      .ignore_timing = false,
      .ignore_sensing = false,
  }};

  // Call function to select port.
  if (!choosePort(midiin, "input"))
    return 0;

  std::cout << "\nReading MIDI input ... press <enter> to quit.\n";
  char input;
  std::cin.get(input);
  return 0;
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return 0;
}
