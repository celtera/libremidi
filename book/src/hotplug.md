# Device connection / disconnection notification

```cpp
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

On CoreMIDI, a removal callback receives the port's metadata as observed when
the endpoint was discovered. This also applies to ports already connected when
the observer starts, even if `notify_in_constructor` is false.

On Windows 10 and later, the automatic WinMM observer subscribes to MIDI input
and output device-interface changes. The first notification schedules a scan
of that direction within 100 ms; further notifications cannot postpone it.
Even without a notification, the worker rechecks each subscribed direction
every two seconds. This catches ports published late and lets the observer
recover from incomplete WinMM snapshots. Only the initial notifications, if
requested, run on the constructing thread.

With a pre-Windows-8 target SDK, on older Windows, or if registration fails,
the worker compares full snapshots at `winmm_observer_configuration::poll_period`.
It does not rely on port counts: replacing one named device with another can
leave the count unchanged. WinMM cannot distinguish physically different devices
with identical observable names. `manual_poll` remains entirely caller-driven.

See `midiobserve.cpp` or `emscripten_midiin.cpp` for an example.
