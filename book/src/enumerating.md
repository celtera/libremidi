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
or finishes discovery: an observer then reports the port removed under its old names and
added under the new ones.

The groups come from the endpoint's function blocks once discovery has finished and all
the blocks it declared have been read; until then, and for devices without function
blocks, they come from its USB group terminal blocks. A device with only inactive function
blocks currently has no ports. An endpoint that describes no block at all has one input
and one output, on group 0.

Three things changed from older versions:

- `port` is the group index, not a block number, and `client` is no longer 0. Discard
  saved identifiers and enumerate again: a full old `port_information` (`client == 0`)
  does not open, and an old integer saved on its own cannot be distinguished from a group
  index. Do not convert it by subtracting one.
- A group input no longer receives the endpoint-wide Utility and Stream messages
  (discovery, function block notifications, jitter-reduction timestamps), which have no
  group.
- Endpoints without blocks are listed, as one group-0 port in each direction.

On output, MIDI 1 byte messages are converted for the selected group. Each successful
open starts fresh conversion state; for example, a previous destination's Bank Select
does not affect a Program Change on the new destination. Explicit UMPs are
sent unchanged to the selected **endpoint**, even when addressed to another group:
their group is part of the caller's packet. A group input receives only messages with
that group field.
