#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>

int main(int argc, char**)
try
{
  libremidi::observer obs;
  auto inputs = obs.get_input_ports();
  auto outputs = obs.get_output_ports();

  if(inputs.size() == 0) {
      std::cerr << "No available input port.\n";
    return -1;
  }
  if(outputs.size() == 0) {
    std::cerr << "No available output port.\n";
    return -1;
  }

  libremidi::midi_out midiout;

  libremidi::midi_in midiin{{
      // Set our callback function.
      .on_message
      = [&](const libremidi::message& message) {
    std::cout << message << std::endl;
    midiout.send_message(message);
      },
  }};

  std::cerr << "Observer? " << outputs[0].display_name << " : " << outputs[0].port_name << std::endl;
  midiout.open_port(outputs[0]);
  midiin.open_port(inputs[0]);

  std::cout << "Creating an echo bridge: " << inputs[0].display_name << " => " << outputs[0].display_name << "\n";
  char input;
  std::cin.get(input);
}
catch (const libremidi::midi_exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
