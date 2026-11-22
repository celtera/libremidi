# Reading MIDI 2 messages from a device through callbacks

Note that the MIDI 1 and MIDI 2 send and receive functions are useable no matter 
the kind of backend used (e.g. one can send UMPs to MIDI 1 backends and MIDI 1 messages to MIDI 2 backends). This conversion is done in a best-effort way.

Note also that libremidi by default will upscale MIDI 1 to MIDI 2 channel events when coming from user-code: it is safe to assume than when using UMP input, only MIDI 2 channel events have to be processed, not MIDI 1 channel events encapsulated into UMPs.

For Windows MIDI Services, a toggle allows to change this behaviour.
When Windows MIDI Services delivers a batch, the input port selects packets by group.
Selected packets share the batch timestamp; the raw callback receives each contiguous run
of selected words without copying them.

```cpp
// Set the configuration of our MIDI port, same warnings apply than for MIDI 1.
// Note that an UMP message is always at most 4 * 32 bits = 16 bytes.
// Added to the 64-bit timestamp this is 24 bytes for a libremidi::ump 
// which is definitely small enough to be passed by value.
// Note that libremidi::ump is entirely constexpr.
auto my_callback = [](libremidi::ump message) {
  // how many 32-bit UMP base elements (e.g. at least 1 uint32_t)
  message.size();
  // access to the individual UMP 
  message[i];
  // access to the timestamp
  message.timestamp;
};

// Create the midi object
libremidi::midi_in midi{ 
  libremidi::ump_input_configuration{ .on_message = my_callback }
};


// Open a given midi port. 
// The argument is a libremidi::input_port gotten from a libremidi::observer. 
midi.open_port(/* a port */);
// Alternatively, to get the default port for the system: 
midi.open_port(libremidi::midi2::in_default_port());

```

## Hosting a Windows MIDI Services virtual port

`open_virtual_port(name)` creates one virtual device per classic input or output:
one static function block, one UMP group (index 0), in the requested direction
from the host's point of view. The display name may be shared; by default each
new virtual device receives a separate, random product instance ID, so its
endpoint device ID changes on every open.

Other applications can only find the same endpoint again across runs if the
identifier stays the same. Set it in the backend configuration:

```cpp
libremidi::winmidi::input_configuration conf;
conf.virtual_endpoint_id = "my_synth-in"; // 1 to 32 characters among [A-Za-z0-9_-]
libremidi::midi_in in{libremidi::ump_input_configuration{/* ... */}, conf};
in.open_virtual_port("My synth");
```

The service compares identifiers without case, and refuses one that a live
virtual device already uses: an input and an output, or two instances of the
application, need different identifiers. `open_virtual_port` returns
`std::errc::invalid_argument` for an identifier the service would reject or cut.

The service publishes the
client-facing endpoint after the host opens its private connection. Closing the
port disconnects it; the same object can then open another port.
If opening fails, libremidi releases its references and disconnects any endpoint
connection it created in the session. If the service already created a private
device, the SDK has no explicit remove operation: libremidi cannot guarantee
that the service removes that device on failure.

The `winmidi_virtual_metadata_test` CTest checks the actual SDK configuration
without creating an endpoint. On Windows with a working virtual transport and a
fix for [Microsoft MIDI #1047](https://github.com/microsoft/MIDI/issues/1047),
configure with `-DLIBREMIDI_TESTS=ON -DLIBREMIDI_TEST_WINMIDI_VIRTUAL=ON` and
run `ctest -R winmidi_virtual_smoke --output-on-failure`. This opt-in test
exchanges UMPs between a host and a separate client and reopens the same host
objects. After each close, including the last, the client checks that both
previously discovered endpoint IDs disappear. The test skips if the service or
virtual transport is unavailable. Availability does not detect #1047; do not
enable the test on an affected system.
