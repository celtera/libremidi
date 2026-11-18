# Enumerating ports

The required header is `#include <libremidi/libremidi.hpp>`.

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

On Windows Multimedia (WinMM), listing ports takes a fresh snapshot of the requested
direction. An observer with no port-added or port-removed callbacks starts no worker
and does not register a manual poll callback. A subscribed observer monitors only
the directions for which a callback was supplied.

WinMM display names number occurrences of the same raw name, starting at 1.
Opening a listed port resolves that occurrence against the current devices; the
old numeric index is not trusted. A selector with only a raw name must match
unambiguously. WinMM cannot distinguish two identical devices that exchange
positions. If a capabilities query fails during a listing, only the prefix whose
names could be numbered reliably is returned.
