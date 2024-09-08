#include <libremidi/libremidi.hpp>

#include <iostream>
#include <mutex>

int main()
try
{
  using namespace libremidi;
  namespace lm2 = libremidi::midi2;
  // The observer object enumerates available inputs and outputs
  observer obs{{}, lm2::observer_default_configuration()};
  auto pi = obs.get_input_ports();
  auto po = obs.get_output_ports();
  if (pi.empty() || po.empty())
    throw std::runtime_error("No MIDI in / out available");

  // Create a midi out
  midi_out midiout{{}, lm2::out_default_configuration()};
  if (auto err = midiout.open_port(po[0]); err != stdx::error{})
    err.throw_exception();

  // Create a midi in
  auto on_ump = [&](const libremidi::ump& message) {
    if (midiout.is_port_connected())
      midiout.send_ump(message);
  };
  midi_in midiin{{.on_message = on_ump}, lm2::in_default_configuration()};

  if (auto err = midiin.open_port(pi[0]); err != stdx::error{})
    err.throw_exception();

  // Wait until we exit
  char input;
  std::cin.get(input);
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
