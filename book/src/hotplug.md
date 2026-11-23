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
and output device-interface changes, whatever Windows version the application
targets. The first notification schedules a scan of that direction within
100 ms; further notifications cannot postpone it. WinMM can publish a port
after its device interface: if that scan finds no change, two more follow,
400 ms and one second after the notification.

An idle observer is not entirely quiet. WinMM can also change without any
notification, so the worker rechecks each subscribed direction every
`winmm_observer_configuration::poll_period`, and at most every two seconds:
a longer `poll_period` spaces these rechecks out, a shorter one does not bring
them closer. Only the initial notifications, if requested, run on the
constructing thread.

On older Windows, in a build that targets Windows XP, or if registration or
waiting for the notifications fails, the worker compares full snapshots at
`poll_period`.
It does not rely on port counts: replacing one named device with another can
leave the count unchanged. WinMM cannot distinguish physically different devices
with identical observable names. `manual_poll` remains entirely caller-driven.

See `midiobserve.cpp` or `emscripten_midiin.cpp` for an example.
