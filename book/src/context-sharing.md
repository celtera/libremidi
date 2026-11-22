# Context sharing

This allows to share a single context across multiple MIDI objects (such as a `jack_client_t` with JACK, a PipeWire main loop & filter, or a `MIDIClientRef` on macOS with CoreMIDI). 
Most backends create a context per object when none is passed. Windows MIDI Services instead
creates a shared session on first open, named after the host executable. Set `client_name`
for a named session, or pass a `MidiSession*` in `context` to use a session you own.
Inputs and outputs share one connection per endpoint within that session. Closing one port
leaves the others connected. A hosted virtual device has its own private plugin connection.

On a shared Windows MIDI Services connection, an exception during input delivery stops
that input's current delivery, not its subscription. If the input is still open, its
`on_error` handler is notified when configured. Other inputs still receive the data;
neither the original exception nor an exception from `on_error` fails the SDK callback.

Closing an ordinary input port from **outside** a Windows MIDI Services receive callback
waits for that port's current delivery to finish. Closing from **inside** a receive callback
disables further delivery but does not wait: if A's callback closes B while B's callback is
running, B can still be executing after close returns. libremidi keeps B's callback object
alive, not objects it references. Capture shared ownership of any state B still needs, or
wait for B to finish outside the receive callback before destroying that state.

Example:

```cpp
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

```cpp
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

In that case, note that the `obs` has ownership of for instance the JACK context object: it must outlive `in` and `out`.

The relevant examples are:
- `coremidi_share.cpp` for a complete example for CoreMIDI.
- `jack_share.cpp` for a complete example for JACK.
- `pipewire_share.cpp` for a complete example for PipeWire.
