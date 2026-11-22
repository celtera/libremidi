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

Windows MIDI Services exposes one input or output per addressed group. To reopen one,
save its `device` (full endpoint device ID), `port` (group **0–15**) and direction.
`display_name` shows groups **1–16** for people and may change when the device is renamed
or finishes discovery. Active function blocks supersede USB group terminal blocks; a
device with only inactive function blocks currently has no ports.

Discard saved block-number identifiers from older versions and enumerate again. A full
old `port_information` (`client == 0`) is rejected; an old integer saved on its own cannot
be distinguished from a group index. Do not convert it by subtracting one.

On output, MIDI 1 byte messages are converted for the selected group. Each successful
open starts fresh conversion state; for example, a previous destination's Bank Select
does not affect a Program Change on the new destination. Explicit UMPs are
sent unchanged to the selected **endpoint**, even when addressed to another group:
their group is part of the caller's packet. A group input receives only messages with
that group field, not endpoint-wide Utility or Stream messages.
