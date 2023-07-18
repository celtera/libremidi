//*****************************************//
//  midiout.cpp
//  by Gary Scavone, 2003-2004.
//
//  Simple program to test MIDI output.
//
//*****************************************//

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

// This function should be embedded in a try/catch block in case of
// an exception.  It offers the user a choice of MIDI ports to open.
// It returns false if there are no ports available.
bool chooseMidiPort(libremidi::midi_out& libremidi)
{
  std::cout << "\nWould you like to open a virtual output port? [y/N] ";

  std::string keyHit;
  std::getline(std::cin, keyHit);
  if (keyHit == "y")
  {
    libremidi.open_virtual_port();
    return true;
  }

  std::string portName;
  unsigned int i = 0, nPorts = libremidi.get_port_count();
  if (nPorts == 0)
  {
    std::cout << "No output ports available!" << std::endl;
    return false;
  }

  if (nPorts == 1)
  {
    std::cout << "\nOpening " << libremidi.get_port_name() << std::endl;
  }
  else
  {
    for (i = 0; i < nPorts; i++)
    {
      portName = libremidi.get_port_name(i);
      std::cout << "  Output port #" << i << ": " << portName << '\n';
    }

    do
    {
      std::cout << "\nChoose a port number: ";
      std::cin >> i;
    } while (i >= nPorts);
  }

  std::cout << "\n";
  libremidi.open_port(i);

  return true;
}

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
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  exit(EXIT_FAILURE);
}
