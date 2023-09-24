# Real-time input/output usage

The required header is `#include <libremidi/libremidi.hpp>`.

## Enumerating ports

Inputs:
```C++
libremidi::observer obs;
for(const libremidi::input_port& port : obs.get_input_ports()) {
  std::cout << port.port_name << "\n";
}
```

Outputs:
```C++
libremidi::observer obs;
for(const libremidi::output_port& port : obs.get_output_ports()) {
  std::cout << port.port_name << "\n";
}
```

See `midiprobe.cpp` for a simple example.

## Reading MIDI 1 messages from a device through callbacks

```C++
// Set the configuration of our MIDI port
// Note that the callback will be invoked from a separate thread,
// it is up to you to protect your data structures afterwards.
// For instance if you are using a GUI toolkit, don't do GUI actions
// in that callback !
auto my_callback = [](const libremidi::message& message) {
  // how many bytes
  message.size();
  // access to the individual bytes
  message[i];
  // access to the timestamp
  message.timestamp;
};

// Create the midi object
libremidi::midi_in midi{ 
  libremidi::input_configuration{ .on_message = my_callback } 
};

// Open a given midi port. 
// The argument is a libremidi::input_port gotten from a libremidi::observer. 
midi.open_port(/* a port */);
// Alternatively, to get the default port for the system: 
midi.open_port(libremidi::midi1::in_default_port());

```

## Sending MIDI 1 messages to a device

```C++
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

## Reading MIDI 2 messages from a device through callbacks

Note that MIDI 2 support is still experimental and subject to change.
Note also that the MIDI 1 and MIDI 2 send / receive functions are useable no matter 
the kind of backend used (e.g. one can send UMPs to MIDI 1 backends and MIDI 1 messages to MIDI 2 backends). This conversion is done in a best-effort way.

```C++
// Set the configuration of our MIDI port, same warnings apply than for MIDI 1.
// Note that an UMP message is always at most 4 * 32 bits = 16 bytes.
// Added to the 64-bit timestamp this is 24 bytes for a libremidi::ump 
// which is definitely small enough to be passed by value.
// Note that libremidi::ump is entirely constexpr.
auto my_callback = [](libremidi::ump message) {
  // how many bytes
  message.size();
  // access to the individual bytes
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

## Sending MIDI 2 messages to a device

```C++
// Create the midi object
libremidi::midi_out midi;

// Open a given midi port. Same as for input:
midi.open_port(libremidi::midi2::out_default_port());

// Option A: send fixed amount of bytes for most basic cases
midi.send_ump(A, B, C, D); // Overloads exist for 1, 2, 3, 4 uint32_t

// Option B: send a raw byte array
uint32_t bytes[2] = { ... };
midi.send_ump(bytes, sizeof(bytes));

// Option C: std::span<uint32_t>
// This allows to pass std::vector, std::array and the likes
midi.send_ump(std::span<uint32_t>{ ... your span-compatible data-structure ... });

// Option D: helpers with the libremidi::ump class
// The helpers haven't been implemented yet :(
midi.send_ump(libremidi::ump{ /* a message */ });
```

## Device connection / disconnection notification

```C++
// The callbacks will be called when the relevant event happens.
// Note that they may be called from other threads than the main thread.

libremidi::observer_configuration conf{
    .input_added = [&] (const libremidi::input_port& id) {
      std::cout << "Input connected: " << id.port_name << std::endl;
    },
    .input_removed = [&] (const libremidi::input_port& id) {
      std::cout << "Input removed: " << id.port_name << std::endl;
    },
    .output_added = [&] (const libremidi::output_port& id) {
      std::cout << "Output connected: " << id.port_name << std::endl;
    },
    .output_removed = [&] (const libremidi::output_port& id) {
      std::cout << "Output removed: " << id.port_name << std::endl;
}};

libremidi::observer obs{std::move(conf)};
```

See `midiobserve.cpp` or `emscripten_midiin.cpp` for an example.

## Error handling

The default error handling is done with exceptions.
If exceptions are undesirable, it is also possible to set a callback function which will be invoked upon error, for the `midi_in` and `midi_out` classes.

(Some classes may still throw, such as when creating invalid MIDI messages with the `libremidi::message` helpers, or the `observer` classes).

```C++
// Create the configuration
libremidi::input_configuration conf{
    .on_message = /* usual message callback */
  , .on_error = [] (libremidi::midi_error code, std::string_view info) {
      // ... log error however you want
    }
  , .on_warning = [] (libremidi::midi_error code, std::string_view info) {
      // ... log warning however you want
    }
};

// Create the midi object
libremidi::midi_in midi{conf};
```

Ditto for `midi_out` and `midi_observer`.

## Queue input

The old queued input mechanism present in RtMidi and previous versions of the library has been moved out of the code as it can be built entirely on the callback mechanism and integrated with the user application's event processing queue instead.

A basic example is provided in `qmidiin.cpp`.

## Advanced configuration

The `midi_in`, `midi_out` and `midi_observer` objects are configured through a `input_configuration` (resp. `output_`, etc.) object passed in argument to the constructor.

### Custom back-end configuration

Additionnally, each back-end supports back-end specific configuration options, to enable users to tap into advanced features of a given API while retaining the general C++ abstraction.

For instance, this allows to share a single context across multiple MIDI objects (such as a `jack_client_t` with JACK or `MIDIClientRef` on macOS with CoreMIDI). 
If no context is passed, each object will create one as they used to.

Example:

```C++
#include <libremidi/configurations.hpp>

...

libremidi::midi_in in{
    libremidi::input_configuration{.on_message = ...}
  , libremidi::alsa_seq::input_configuration{
      .client_name = "my client"
  } 
};
```

If one simply wants to share a context across libremidi objects (for instance, a single context shared across an `observer`, `midi_ins` and `midi_outs`), the following methods will create appropriate configurations from an observer's configuration: 

```C++
// Create an observer with a fixed back-end
libremidi::observer obs{
    libremidi::observer_configuration{}
  , libremidi::observer_configuration_for(libremidi::API::JACK_MIDI)};

// The in and out will share the JACK client of the observer.
// Note that the observer has to outlive them.
libremidi::midi_in in{
    libremidi::input_configuration{.on_message = ...}
  , libremidi::midi_in_configuration_for(obs) 
};

libremidi::midi_out out{
    libremidi::output_configuration{...}
  , libremidi::midi_out_configuration_for(obs) 
};
```

See:
- `coremidi_share.cpp` for a complete example for CoreMIDI.
- `jack_share.cpp` for a complete example for JACK.

### Custom polling

Traditionnally, RtMidi (and most other MIDI libraries) opened a dedicated MIDI thread from which messages are read, and then transferred to the rest of the app.

If your app is based on an event loop that can poll file descriptors, such as `poll()`, the relevant back-ends will allow to instead control polling manually by providing you with the file descriptors, 
which can then be injected into your app's main loop. 

Thus, this enables complete control over threading (and can also remove the need for synchronisation as this allows to make a callback-based yet single-threaded app, for simple applications which do not wish 
to reimplement MIDI filtering & parsing for back-ends such as ALSA Sequencer or RawMidi).

Since this feature is pretty complicated to implement, we recommend checking the examples: 

See:
- `poll_share.cpp` for a complete example for ALSA RawMidi (recommended).
- `alsa_share.cpp` for a complete example for ALSA Seq.