#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char**)
try
{
  libremidi::observer obs;
  auto inputs = obs.get_input_ports();
  auto outputs = obs.get_output_ports();

  if (inputs.size() == 0)
  {
    std::cerr << "No available input port.\n";
    return -1;
  }
  if (outputs.size() == 0)
  {
    std::cerr << "No available output port.\n";
    return -1;
  }

  libremidi::midi_out midiout{
      libremidi::output_configuration{}, libremidi::midi_out_configuration_for(obs)};

  libremidi::midi_in midiin{
      {
          // Set our callback function.
          .on_message
          = [&](const libremidi::message& message) {
    std::cout << message << std::endl;
    midiout.send_message(message);
          },
      },
      libremidi::midi_in_configuration_for(obs)};

  midiout.open_port(outputs[0]);
  if (!midiout.is_port_connected())
  {
    std::cerr << "Could not connect to midi out\n";
    return -1;
  }
  midiin.open_port(inputs[0]);
  if (!midiin.is_port_connected())
  {
    std::cerr << "Could not connect to midi in\n";
    return -1;
  }

  std::cout << "Creating an echo bridge: " << inputs[0].display_name << " => "
            << outputs[0].display_name << "\n";
  while (1)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  char input;
  std::cin.get(input);
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
