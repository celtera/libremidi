# Sending MIDI 2 messages to a device

```cpp
// Create the midi object
libremidi::midi_out midi;

// Open a given midi port. Same as for input:
if(auto port = libremidi::midi2::out_default_port())
  midi.open_port(*port);

// Option A: send one complete packet, with one argument per 32-bit word.
midi.send_ump(A, B, C, D); // Overloads exist for 1, 2, 3, 4 uint32_t

// Option B: send an array of MIDI words. The count is in words, not bytes.
uint32_t words[2] = { ... };
midi.send_ump(words, std::size(words));

// Option C: send the same words as a span.
midi.send_ump(std::span<const uint32_t>{words});

// Option D: send one packet using libremidi::ump.
midi.send_ump(libremidi::ump{ /* a message */ });
```

The pointer/count and span overloads take a nonempty stream of complete UMPs
in order. A packet's message type determines its length (one to four words);
the stream as a whole is not limited to four words. A send is not atomic:
an error does not undo packets the backend has already sent. A packet cut short
by the end of the stream gives `bad_message`, after the complete packets before
it were sent. Two backends do not look at the words: raw ALSA writes them to the
device as given, and raw I/O passes them to the `write_ump` callback.

On Windows MIDI Services, a raw output connection batches contiguous, complete
UMPs: at most 171 words per batch, or the connection's own limit if it is lower.
The connection accepts four times as much, but the service translates a batch
for MIDI 1 clients and drops one that no longer fits its buffers once
translated. The service delivers a batch as a unit, at least once: when it
retries a delivery, the receiver gets the whole batch again.
Zero NOOP/padding words between packets
are skipped, not zero words inside packets. A truncated packet, a packet larger
than the limit, or a failed transmission stops the send and returns an error.
Earlier batches may already have reached the endpoint; they are never retried.
The WinRT fallback, including the hosting side of a virtual port, sends one
complete packet at a time. It likewise skips boundary padding and stops at a
truncated packet (`bad_message`) or a failed write (`io_error`).
