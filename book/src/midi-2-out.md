# Sending MIDI 2 messages to a device

```cpp
// Create the midi object
libremidi::midi_out midi;

// Open a given midi port. Same as for input:
if(auto port = libremidi::midi2::out_default_port())
  midi.open_port(*port);

// Option A: send fixed amount of bytes for most basic cases
midi.send_ump(A, B, C, D); // Overloads exist for 1, 2, 3, 4 uint32_t

// Option B: send an array of MIDI words. The count is in words, not bytes.
uint32_t words[2] = { ... };
midi.send_ump(words, std::size(words));

// Option C: std::span<uint32_t>
// This allows to pass std::vector, std::array and the likes
// This can contain a sequence of UMP messages.
midi.send_ump(std::span<uint32_t>{ ... your span-compatible data-structure ... });

// Option D: helpers with the libremidi::ump class
// The helpers haven't been implemented yet :(
midi.send_ump(libremidi::ump{ /* a message */ });
```

On Windows MIDI Services, a raw output connection batches contiguous, complete
UMPs up to the connection's word limit. Zero NOOP/padding words between packets
are skipped, not zero words inside packets. A truncated packet, a packet larger
than the limit, or a failed transmission stops the send and returns an error.
Earlier batches may already have reached the endpoint; they are never retried.
The hosting side of a virtual port continues to send individual WinRT packets.
