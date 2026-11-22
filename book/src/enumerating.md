# Enumerating ports

The required header is `#include <libremidi/libremidi.hpp>`.

On Windows Multimedia (WinMM), listing ports takes a fresh snapshot of the requested
direction. An observer with no port-added or port-removed callbacks starts no worker
and does not register a manual poll callback. A subscribed observer monitors only
the directions for which a callback was supplied. The first snapshot in each
direction establishes its baseline: existing ports are reported only if
`notify_in_constructor` is true.

WinMM display names number occurrences of the same raw name, starting at 1.
The old numeric index is not trusted when opening a port. The port is resolved
against the current devices, in this order:

1. The occurrence that the display name designates, when the display name has
   the listed form and agrees with the raw name, if one is given.
2. Otherwise, or if that occurrence is no longer there, the first device whose
   raw name is `port_name`. Without a `port_name`, the display name is tried as
   a raw name, then the name it numbers.

At each step, if the selector carries WinMM manufacturer and product IDs and
several devices share the name, one with these IDs is preferred. Different IDs
never prevent the port from opening: many drivers report generic ones. WinMM
cannot distinguish two identical devices that exchange positions.

If either ID changes for a numbered name, the observer reports removal of the
previous selector and addition of the new one. A device whose capabilities
cannot be queried is left out of listings and observer snapshots, and takes no
occurrence number; the other devices are still reported.

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
