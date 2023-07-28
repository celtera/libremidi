#include "utils.hpp"

#include <libremidi/backends/backends.hpp>
#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>

// Credits to https://raw.githubusercontent.com/atsushieno/cmidi2
#include "cmidi2.hpp"

std::ostream& operator <<(std::ostream& s, const libremidi::ump& message)
{
  auto b = (cmidi2_ump*)&message.bytes[0];
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
  libremidi::observer obs{{}, libremidi::coremidi_ump::observer_configuration{}};

  libremidi::midi_in midiin{
      {
          // Set our callback function.
          .on_message
          = [](const libremidi::ump& message) {
    std::cout << message << std::endl;
          },

          // Don't ignore sysex, timing, or active sensing messages.
          .ignore_sysex = false,
          .ignore_timing = false,
          .ignore_sensing = false,
      },
      libremidi::coremidi_ump::input_configuration{}};

  auto p = obs.get_input_ports();
  if (p.empty())
  {
    std::cerr << "No device found\n";
    return -1;
  }

  for (const auto& port : p)
  {
    std::cout << port.port_name << " | " << port.display_name << std::endl;
  }
  midiin.open_port(p[0]);
  std::cout << "\nReading MIDI input ... press <enter> to quit.\n";
  char input;
  std::cin.get(input);
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
