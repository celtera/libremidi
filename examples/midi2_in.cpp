#include "utils.hpp"

#include <libremidi/libremidi.hpp>

// Credits to https://raw.githubusercontent.com/atsushieno/cmidi2
#include <libremidi/cmidi2.hpp>

#include <cstdlib>
#include <iostream>

std::ostream& operator <<(std::ostream& s, const libremidi::ump& message)
{
  auto b = const_cast<uint32_t*>(&message.bytes[0]);
  int bytes = cmidi2_ump_get_num_bytes(message.bytes[0]);
  int group = cmidi2_ump_get_group(b);
  int status = cmidi2_ump_get_status_code(b);
  int channel = cmidi2_ump_get_channel(b);
  s << "[ "  << bytes << " | " << group;

  switch((libremidi::message_type)status) {
    case libremidi::message_type::NOTE_ON:
      s <<   " | note on: " <<  channel << (int)cmidi2_ump_get_midi2_note_note(b)<< " | " << cmidi2_ump_get_midi2_note_velocity(b);
      break;
    case libremidi::message_type::NOTE_OFF:
      s <<   " | note off: " <<  channel << (int)cmidi2_ump_get_midi2_note_note(b)<< " | " << cmidi2_ump_get_midi2_note_velocity(b);
      break;
    case libremidi::message_type::CONTROL_CHANGE:
      s << " | cc: " <<  channel << (int)cmidi2_ump_get_midi2_cc_index(b)<< " | " << cmidi2_ump_get_midi2_cc_data(b);
      break;

    default:
      break;
  }
  s << " ]";
  return s;
}

int main()
try
{
  libremidi::observer obs{{}, libremidi::midi2::observer_default_configuration()};
  libremidi::midi_out midiout{{}, libremidi::midi2::out_default_configuration()};

  libremidi::midi_in midiin{
      {
          // Set our callback function.
          .on_message
          = [&](const libremidi::ump& message) {
    std::cout << message << std::endl;
    if(midiout.is_port_connected())
      midiout.send_ump(message);
          }
      },
      libremidi::midi2::in_default_configuration()};

  auto pi = obs.get_input_ports();
  if (pi.empty())
  {
    std::cerr << "No MIDI 2 device found\n";
    return -1;
  }

  for (const auto& port : pi)
  {
    std::cout << "In: " << port.port_name << " | " << port.display_name << std::endl;
  }

  auto po = obs.get_output_ports();
  for (const auto& port : po)
  {
    std::cout << "Out: " << port.port_name << " | " << port.display_name << std::endl;
  }
  midiin.open_port(pi[0]);

  if(!po.empty())
    midiout.open_port(po[0]);
  std::cout << "\nReading MIDI input ... press <enter> to quit.\n";

  char input;
  std::cin.get(input);
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
