# Error handling

The default error handling is done with exceptions.
If exceptions are undesirable, it is also possible to set a callback function which will be invoked upon error, for the `midi_in` and `midi_out` classes.

(Some classes may still throw, such as when creating invalid MIDI messages with the `libremidi::message` helpers, or the `observer` classes).

```cpp
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
