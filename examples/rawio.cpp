#include "utils.hpp"

#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>

/// This example demonstrates the raw I/O backend, which allows
/// the user to plug in their own byte-level transport functions.
/// This is useful for Arduino, Teensy, ESP32, serial ports, SPI, USB HID, etc.
///
/// In this example we simulate a loopback: bytes written by the output
/// are fed directly into the input, as if connected by a serial wire.
int main()
{
  // MIDI 1.0 raw I/O example
  {
    std::cout << "=== MIDI 1 Raw I/O ===" << std::endl;

    // The receive callback will be stored here by the library
    libremidi::rawio_input_configuration::receive_callback feed_input;

    // Create a MIDI input that prints received messages
    libremidi::midi_in midiin{
        libremidi::input_configuration{
            .on_message
            = [](const libremidi::message& m) {
      std::cout << "Received MIDI 1: " << m << std::endl;
    }},
        libremidi::rawio_input_configuration{
            .set_receive_callback = [&](auto cb) { feed_input = std::move(cb); },
            .stop_receive = [&] { feed_input = nullptr; }}};

    // Create a MIDI output whose write function loops back to the input
    libremidi::midi_out midiout{
        libremidi::output_configuration{},
        libremidi::rawio_output_configuration{
            .write_bytes = [&](std::span<const uint8_t> bytes) -> stdx::error {
      // Simulate a serial loopback: bytes go directly to the input
      if (feed_input)
        feed_input(bytes, 0);
      return {};
    }}};

    midiin.open_virtual_port("rawio_in");
    midiout.open_virtual_port("rawio_out");

    // Send some MIDI messages
    midiout.send_message(0x90, 60, 100); // Note On: C4, velocity 100
    midiout.send_message(0x80, 60, 0);   // Note Off: C4
    midiout.send_message(0xB0, 7, 80);   // CC: Volume = 80
  }

  // MIDI 2.0 (UMP) raw I/O example
  {
    std::cout << "\n=== MIDI 2 Raw I/O (UMP) ===" << std::endl;

    libremidi::rawio_ump_input_configuration::receive_callback feed_input;

    libremidi::midi_in midiin{
        libremidi::ump_input_configuration{.on_message = [](const libremidi::ump& m) {
          std::cout << "Received UMP: " << m << std::endl;
        }},
        libremidi::rawio_ump_input_configuration{
            .set_receive_callback = [&](auto cb) { feed_input = std::move(cb); },
            .stop_receive = [&] { feed_input = nullptr; }}};

    libremidi::midi_out midiout{
        libremidi::output_configuration{},
        libremidi::rawio_ump_output_configuration{
            .write_ump = [&](std::span<const uint32_t> words) -> stdx::error {
      if (feed_input)
        feed_input(words, 0);
      return {};
    }}};

    midiin.open_virtual_port("rawio_ump_in");
    midiout.open_virtual_port("rawio_ump_out");

    // Send a MIDI 2.0 Note On UMP (type 4, group 0, channel 0, note 60)
    uint32_t ump[2] = {0x40900000 | 60, 0xC0000000};
    midiout.send_ump(ump, 2);
  }

  return 0;
}
