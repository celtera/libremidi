#pragma once

#include <libremidi/libremidi.hpp>
// Credits to https://raw.githubusercontent.com/atsushieno/cmidi2
#include <libremidi/cmidi2.hpp>
#if LIBREMIDI_USE_NI_MIDI2
  #include <midi/universal_packet.h>
#endif

#include <cstdlib>
#include <iostream>

inline std::ostream& operator<<(std::ostream& s, const libremidi::message& message)
{
  auto nBytes = message.size();
  s << "[ ";
  for (auto i = 0U; i < nBytes; i++)
    s << std::hex << (int)message[i] << std::dec << " ";
  s << "]";
  if (nBytes > 0)
    s << " ; stamp = " << message.timestamp;
  return s;
}

inline std::ostream& operator<<(std::ostream& s, const libremidi::ump& message)
{
  const cmidi2_ump* b = message;
  int bytes = cmidi2_ump_get_num_bytes(message.data[0]);
  int group = cmidi2_ump_get_group(b);
  int status = cmidi2_ump_get_status_code(b);
  int channel = cmidi2_ump_get_channel(b);
  s << "[ " << bytes << " | " << group;

  switch ((libremidi::message_type)status)
  {
    case libremidi::message_type::NOTE_ON:
      s << " | note on: " << channel << (int)cmidi2_ump_get_midi2_note_note(b) << " | "
        << cmidi2_ump_get_midi2_note_velocity(b);
      break;
    case libremidi::message_type::NOTE_OFF:
      s << " | note off: " << channel << (int)cmidi2_ump_get_midi2_note_note(b) << " | "
        << cmidi2_ump_get_midi2_note_velocity(b);
      break;
    case libremidi::message_type::CONTROL_CHANGE:
      s << " | cc: " << channel << (int)cmidi2_ump_get_midi2_cc_index(b) << " | "
        << cmidi2_ump_get_midi2_cc_data(b);
      break;

    default:
      break;
  }
  s << " ]";
#if LIBREMIDI_USE_NI_MIDI2
  s << " :: " << midi::universal_packet(message);
#endif
  return s;
}

inline std::ostream& operator<<(std::ostream& s, const libremidi::port_information& rhs)
{
  s << "[ client: " << rhs.client << ", port: " << rhs.port;
  if (!rhs.manufacturer.empty())
    s << ", manufacturer: " << rhs.manufacturer;
  if (!rhs.device_name.empty())
    s << ", device: " << rhs.device_name;
  if (!rhs.port_name.empty())
    s << ", portname: " << rhs.port_name;
  if (!rhs.display_name.empty())
    s << ", display: " << rhs.display_name;
  return s << "]";
}

// This function should be embedded in a try/catch block in case of
// an exception.  It offers the user a choice of MIDI ports to open.
// It returns false if there are no ports available.
inline bool chooseMidiPort(libremidi::midi_in& libremidi)
{
  std::cout << "\nWould you like to open a virtual input port? [y/N] ";

  std::string keyHit;
  std::getline(std::cin.ignore(), keyHit);
  if (keyHit == "y")
  {
    libremidi.open_virtual_port();
    return true;
  }

  std::string portName;
  auto ports = libremidi::observer{{}, observer_configuration_for(libremidi.get_current_api())}
                   .get_input_ports();
  unsigned int i = 0;
  std::size_t nPorts = ports.size();
  if (nPorts == 0)
  {
    std::cout << "No input ports available!" << std::endl;
    return false;
  }

  if (nPorts == 1)
  {
    std::cout << "\nOpening " << ports[0].display_name << std::endl;
  }
  else
  {
    for (i = 0; i < nPorts; i++)
    {
      portName = ports[i].display_name;
      std::cout << "  Input port #" << i << ": " << portName << '\n';
    }

    do
    {
      std::cout << "\nChoose a port number: ";
      std::cin >> i;
    } while (i >= nPorts);
  }

  std::cout << "\n";
  libremidi.open_port(ports[i]);

  getchar();
  return true;
}

inline bool chooseMidiPort(libremidi::midi_out& libremidi)
{
  std::cout << "\nWould you like to open a virtual output port? [y/N] ";

  std::string keyHit;
  std::getline(std::cin.ignore(), keyHit);
  if (keyHit == "y")
  {
    libremidi.open_virtual_port();
    return true;
  }

  std::string portName;
  auto ports = libremidi::observer{{}, observer_configuration_for(libremidi.get_current_api())}
                   .get_output_ports();
  unsigned int i = 0;
  std::size_t nPorts = ports.size();
  if (nPorts == 0)
  {
    std::cout << "No output ports available!" << std::endl;
    return false;
  }

  if (nPorts == 1)
  {
    std::cout << "\nOpening " << ports[0].display_name << std::endl;
  }
  else
  {
    for (i = 0; i < nPorts; i++)
    {
      portName = ports[i].display_name;
      std::cout << "  Output port #" << i << ": " << portName << '\n';
    }

    do
    {
      std::cout << "\nChoose a port number: ";
      std::cin.ignore() >> i;
    } while (i >= nPorts);
  }

  std::cout << "\n";
  libremidi.open_port(ports[i]);

  return true;
}
