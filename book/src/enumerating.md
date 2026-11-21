# Enumerating ports

The required header is `#include <libremidi/libremidi.hpp>`.

On Windows Multimedia (WinMM), listing ports takes a fresh snapshot of the requested
direction. An observer with no port-added or port-removed callbacks starts no worker
and does not register a manual poll callback. A subscribed observer monitors only
the directions for which a callback was supplied. The first complete snapshot in
each direction establishes its baseline: existing ports are reported only if
`notify_in_constructor` is true, even if a failed query delayed that snapshot.

WinMM display names number occurrences of the same raw name, starting at 1.
Opening a listed port resolves that occurrence against the current devices and
checks the WinMM manufacturer and product IDs; the old numeric index is not
trusted. If either ID changes for a numbered name, the observer reports removal
of the previous selector and addition of the new one. A selector with only a raw
name must match unambiguously. WinMM cannot distinguish two identical devices
that exchange positions. If a capabilities query fails during a listing, only
the prefix whose names could be numbered reliably is returned.

Inputs:
```cpp
libremidi::observer obs;
for(const libremidi::input_port& port : obs.get_input_ports()) {
  std::cout << port.port_name << "\n";
}
```

Outputs:
```cpp
libremidi::observer obs;
for(const libremidi::output_port& port : obs.get_output_ports()) {
  std::cout << port.port_name << "\n";
}
```

See `midiprobe.cpp` for a simple example.
