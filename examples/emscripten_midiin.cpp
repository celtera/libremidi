#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

#include <emscripten.h>

#include <array>
#include <iostream>
#include <memory>

/**
 * Note: due to Javascript being mostly async,
 * we need to use things in an async way here too.
 */
int main(int argc, char**)
{
  std::vector<std::shared_ptr<libremidi::midi_in>> inputs;
  std::vector<std::shared_ptr<libremidi::midi_out>> outputs;

  libremidi::observer_configuration callbacks{
      .input_added =
          [&](const libremidi::port_information& id) {
    std::cout << "MIDI Input connected: " << id.port_name << std::endl;

    auto conf = libremidi::input_configuration{
        .on_message = [](const libremidi::message& msg) { std::cout << msg << std::endl; }};
    auto input
        = std::make_shared<libremidi::midi_in>(conf, libremidi::emscripten_input_configuration{});

    input->open_port(id);

    inputs.push_back(input);
      },

      .input_removed =
          [&](const libremidi::port_information& id) {
    std::cout << "MIDI Input removed: " << id.port_name << std::endl;
      },

      .output_added
      = [&](const libremidi::port_information& id) {
    std::cout << "MIDI Output connected: " << id.port_name << std::endl;

    libremidi::midi_out output{};
    output.open_port(id);
    output.send_message(0x90, 64, 100);
    output.send_message(0x80, 64, 100);
      },

      .output_removed = [&](const libremidi::port_information& id) {
        std::cout << "MIDI Output removed: " << id.port_name << std::endl;
      }};

  libremidi::observer obs{libremidi::API::WEBMIDI, std::move(callbacks)};

  emscripten_set_main_loop([] {}, 60, 1);
}
