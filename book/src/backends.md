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

WinMIDI targets the [in-box Windows MIDI Services API](https://microsoft.github.io/MIDI/kb/porting-midi-libraries/). It requires an x64 or Arm64 process on Windows 11 25H2 (build 26200) or later, registration of the in-box API, and an available MIDI service. On other systems use WinMM or WinUWP. Initialize the caller's WinRT apartment before querying availability or constructing MIDI objects.

During the preview, CMake generates the API projections from the pinned `Windows.Devices.Midi2 0.99.92-preview.11` NuGet package; no runtime DLL is deployed. Install a Windows SDK (for the platform metadata and C++/WinRT) or use the MSYS2 toolchain’s matching cppwinrt headers and generator with Windows SDK metadata. For offline builds, set `LIBREMIDI_WINMIDI_HEADERS_ZIP` to the local package. Set `LIBREMIDI_DOWNLOAD_CPPWINRT=OFF` to prevent downloads or `LIBREMIDI_NO_WINMIDI=ON` to disable the backend entirely.

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
