//*****************************************//
//  sysextest.cpp
//  by Gary Scavone, 2003-2005.
//
//  Simple program to test MIDI sysex sending and receiving.
//
//*****************************************//

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

// This function should be embedded in a try/catch block in case of
// an exception.  It offers the user a choice of MIDI ports to open.
// It returns false if there are no ports available.

bool chooseMidiPort(libremidi::midi_in& libremidi)
{
  std::cout << "\nWould you like to open a virtual input port? [y/N] ";

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
    std::cout << "No input ports available!" << std::endl;
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
      std::cout << "  Input port #" << i << ": " << portName << '\n';
    }

    do
    {
      std::cout << "\nChoose a port number: ";
      std::cin >> i;
    } while (i >= nPorts);
  }

  std::cout << std::endl;
  libremidi.open_port(i);

  return true;
}

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

  std::cout << std::endl;
  libremidi.open_port(i);

  return true;
}

int main(int argc, char* argv[])
try
{
  using namespace std::literals;
  libremidi::midi_out midiout;
  libremidi::midi_in midiin{libremidi::input_configuration{
      // Set our callback function.
      .on_message
      = [](const libremidi::message& message) {
    auto nBytes = message.size();
    for (auto i = 0U; i < nBytes; i++)
      std::cout << "Byte " << i << " = " << (int)message[i] << ", ";
    if (nBytes > 0)
      std::cout << "stamp = " << message.timestamp << std::endl;
      },

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
catch (libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return 0;
}
