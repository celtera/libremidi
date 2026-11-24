# Context sharing

This allows to share a single context across multiple MIDI objects (such as a `jack_client_t` with JACK, a PipeWire main loop & filter, or a `MIDIClientRef` on macOS with CoreMIDI). 
Most backends create a context per object when none is passed. Windows MIDI Services instead
creates a shared session on first open, named after the host executable. Set `client_name`
for a named session, or pass a `MidiSession*` in `context` to use a session you own.
Inputs and outputs share one connection per endpoint within that session. Closing one port
leaves the others connected. A hosted virtual device has its own private plugin connection.

`client_name` is empty by default in the Windows MIDI Services configurations; it used to be
`"libremidi input"` and `"libremidi output"`, with one session per object. An empty name
selects the session of the process: it is created by the first port that opens, and stays
open until the process exits, also after the last port closed and after a module that embeds
libremidi was unloaded. Objects with the same non-empty name share one session, which is
closed with the last of them. libremidi never closes a session passed in `context`.

When opening a port fails on a shared session, libremidi tries once on a new session, and
shares that one from then on if it works: a session does not survive a restart of the MIDI
service. Ports that were open before stay on the previous session and have to be reopened.

A shared connection does not survive its device: when the device is unplugged, libremidi
disconnects it, and sending on its ports reports an error. Reopen the ports when the
observer reports the device again. The same holds for a connection that was disconnected
in the session behind libremidi's back, for instance through
`MidiSession::DisconnectEndpointConnection` on a session you own. In both cases the next
port opened on that endpoint creates a new connection, on which the inputs that were open
continue to receive. Outputs opened before have to be reopened.

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

Opening a port from **inside** a receive callback fails with
`std::errc::resource_unavailable_try_again` while the connection of that endpoint is being
opened or closed, since that operation may itself be waiting for the callback to return.
This always happens when a callback closes the last port of an endpoint and opens it again:
the connection is only released once the callback has returned. Open it again from another
thread, or after the callback.

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
