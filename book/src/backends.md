# Feature matrix

This table shows which feature is supported by which backend so far, for advanced features.

It may be because the backend does not provide the ability at all (N/A), 
or because it has not been implemented yet.

Name lookups and the `*_configuration_for` helpers describe compiled backends;
they do not check service or device availability. Use `available_apis()` or
`available_ump_apis()` to query runtime availability. An explicit request for an
unavailable backend reports an error rather than selecting another backend.
Use `API::UNSPECIFIED` to try available backends in preference order.

## Linux & BSD

|               | ALSA Raw | ALSA Seq | PipeWire |
|---------------|----------|----------|----------|
| MIDI 1        | Yes      | Yes      | Yes      |
| MIDI 2        | Yes      | Yes      | N/A      |
| Virtual ports | N/A      | Yes      | Yes      |
| Observer      | Yes      | Yes      | Yes      |
| Scheduling    | No       | No       | No       |


### Special features
- The ALSA Raw back-end allows to perform chunked sending of MIDI messages, 
which can be useful to upload firmwares.

- `libasound` and `libpipewire` are always loaded through `dlopen`.
JACK can also be, optionally.

This allows libremidi to be built on a system with e.g. PipeWire support 
without preventing application loading if the end user does not use it.

## Windows 

|               | WinMM | UWP | WinMIDI |
|---------------|-------|-----|---------|
| MIDI 1        | Yes   | Yes | No      |
| MIDI 2        | N/A   | N/A | Yes     |
| Virtual ports | N/A   | No  | No      |
| Observer      | Yes   | Yes | Yes     |
| Scheduling    | No    | No  | No      |

WinMIDI targets the [in-box Windows MIDI Services API](https://microsoft.github.io/MIDI/kb/porting-midi-libraries/). It requires an x64 or Arm64 process on Windows 11 25H2 (build 26200) or later, the `Windows.Devices.Midi2` runtime, and an available MIDI service. On other systems use WinMM or WinUWP.

### Runtime

libremidi neither installs nor deploys `Windows.Devices.Midi2.dll`. Until Windows ships it, no system has it: deploy the DLL from the `Windows.Devices.Midi2` NuGet package (`runtimes/win-<arch>/native`) next to your executable. The loader looks in the directory of the executable and in the system directory, not in the directory of the DLL that contains libremidi: a plug-in cannot bring its own copy, the host application has to provide it.

Without the runtime the backend reports itself as unavailable and is skipped by automatic selection.

### Availability and selection

- `libremidi::available_ump_apis()` and the default configuration helpers only check that the runtime is present. They never start the MIDI service.
- Constructing a `midi_in`, `midi_out` or `observer` for this backend starts the service if needed, which can take a few seconds the first time.
- Initialize the caller's WinRT or COM apartment before either of them. On a thread that did not, the check leaves the process with an implicit multi-threaded apartment.
- `midi2::in_default_configuration()`, `out_default_configuration()`, `observer_default_configuration()` and the `midi1::` ones give the platform's preferred backend when it is compiled in and available, and otherwise let the constructor pick the first backend that works.
- A backend requested explicitly, through a `libremidi::API` value or one of its configuration types, is never replaced by another one: when it cannot be used, the `on_error` callback is called and the object does nothing (`get_current_api()` is `API::DUMMY`). The C API returns `-ENOTSUP` from the `*_new` function instead.

### Building

CMake generates the API projection from the pinned `Windows.Devices.Midi2 0.99.92-preview.11` NuGet package (about 100 MB, downloaded at configure time). If the download fails, a warning is printed and libremidi is built without this backend.

| CMake variable | Role |
|---|---|
| `LIBREMIDI_NO_WINMIDI=ON` | Disables the backend. |
| `LIBREMIDI_DOWNLOAD_CPPWINRT=OFF` | Forbids the download. The backend is then only built with `LIBREMIDI_WINMIDI_HEADERS_ZIP`. |
| `LIBREMIDI_WINMIDI_HEADERS_ZIP` | Path of a local copy of the NuGet package, for offline builds. |
| `CPPWINRT_PATH` | Directory of the C++/WinRT base headers (`winrt/base.h`). Found in the Windows SDK or in the toolchain (MSYS2) by default. |
| `CPPWINRT_TOOL` | The `cppwinrt` generator. It must be the release that produced the headers in `CPPWINRT_PATH`. |

The backend is not built for 32-bit targets.

The generated headers are installed in `include/libremidi-winmidi`, without `winrt/base.h`. They only compile against the C++/WinRT release that generated them: an application that uses another one (for instance the `Microsoft.Windows.CppWinRT` NuGet package, or another Windows SDK) gets a "Mismatched C++/WinRT headers" error. Build libremidi with that application's `CPPWINRT_PATH` and `CPPWINRT_TOOL` in this case.

### Changes from the out-of-box SDK

- `winmidi::input_configuration::context` and `winmidi::output_configuration::context` are now `winrt::Windows::Devices::Midi2::MidiSession*` instead of `winrt::Microsoft::Windows::Devices::Midi2::MidiSession*`.
- The `for_all_backends` and `for_backend` helpers of `<libremidi/backends.hpp>` are replaced by `for_available_backends`, `for_compiled_backends` and `for_compiled_backend`.

## Mac & iOS

|               | CoreMIDI |
|---------------|----------|
| MIDI 1        | Yes      |
| MIDI 2        | Yes      |
| Virtual ports | Yes      |
| Observer      | Yes      |
| Scheduling    | No       |

## Web

|               | Emscripten WebMIDI |
|---------------|--------------------|
| MIDI 1        | Yes                |
| MIDI 2        | N/A                |
| Virtual ports | N/A                |
| Observer      | Yes                |
| Scheduling    | No                 |

## Shared backends

|               | JACK |
|---------------|------|
| MIDI 1        | Yes  |
| MIDI 2        | N/A  |
| Virtual ports | Yes  |
| Observer      | Yes  |
| Scheduling    | No   |
