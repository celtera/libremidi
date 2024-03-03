# Sending MIDI 1 messages to a device

```cpp
// Create the midi object
libremidi::midi_out midi;

// Open a given midi port. Same as for input:
midi.open_port(libremidi::midi2::out_default_port());

// Option A: send fixed amount of bytes for most basic cases
midi.send_message(144, 110, 40); // Overloads exist for 1, 2, 3 bytes

// Option B: send a raw byte array
unsigned char bytes[3] = { 144, 110, 40 };
midi.send_message(bytes, sizeof(bytes));

// Option C: std::span<unsigned char>
// This allows to pass std::vector, std::array and the likes
midi.send_message(std::span<unsigned char>{ ... your span-compatible data-structure ... });

// Option D: helpers with the libremidi::message class
// See libremidi/message.hpp for the full list
midi.send_message(libremidi::message::note_on(channel, note, velocity));
midi.send_message(libremidi::message::control_change(channel, control, value));
midi.send_message(libremidi::message::pitch_bend(channel, value));
midi.send_message(libremidi::message{ /* a message */ });
```
