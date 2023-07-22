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

  libremidi::observer::callbacks callbacks{
      .input_added =
          [&](int idx, const std::string& id) {
    std::cout << "MIDI Input connected: " << idx << " - " << id << std::endl;

    auto conf = libremidi::input_configuration{.on_message = [](const libremidi::message& msg) {
      std::cout << (int)msg.bytes[0] << " " << (int)msg.bytes[1] << " " << (int)msg.bytes[2] << " "
                << (double)msg.timestamp << std::endl;
    }};
    auto input
        = std::make_shared<libremidi::midi_in>(conf, libremidi::emscripten_input_configuration{});

    input->open_port(idx);

    inputs.push_back(input);
      },

      .input_removed =
          [&](int idx, const std::string& id) {
    std::cout << "MIDI Input removed: " << id << std::endl;
      },

      .output_added
      = [&](int idx, const std::string& id) {
    std::cout << "MIDI Output connected: " << idx << " - " << id << std::endl;

    libremidi::midi_out output{};
    output.open_port(idx);
    output.send_message(std::array<unsigned char, 3>{0x90, 64, 100});
    output.send_message(std::array<unsigned char, 3>{0x80, 64, 100});
      },

      .output_removed = [&](int idx, const std::string& id) {
        std::cout << "MIDI Output removed: " << id << std::endl;
      }};

  libremidi::observer obs{libremidi::API::EMSCRIPTEN_WEBMIDI, std::move(callbacks)};

  std::cerr << "Ooof!!! \n";
  emscripten_set_main_loop([] {}, 60, 1);

  std::cerr << "Owww!! \n";
}
