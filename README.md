# libremidi

[![Build status](https://github.com/jcelerier/libremidi/workflows/Build/badge.svg)](https://github.com/jcelerier/libremidi/actions)

libremidi is a cross-platform C++20 library for real-time and MIDI file input and output.

This is a fork / rewrite based on two libraries: 

* [RtMidi](https://github.com/theSTK/RtMidi)
* [ModernMIDI](https://github.com/ddiakopoulos/ModernMIDI)

Additionnally, for MIDI 2 parsing support we use [cmidi2](https://github.com/atsushieno/cmidi2)!

## Changelog 

### Since v4
* Experimental MIDI 2.0 support. For now only for CoreMIDI (input / output) and ALSA raw (input, with kernel 6.5+). 
  * More backends to come!
* A neat configuration system which enables to pass options to the underlying backends
* Possibility to share the contexts across inputs and outputs to avoid creating multiple clients in e.g. JACK
* Hotplug support for all the backends!
* Reworked port opening API which now uses handles instead of port indices to increase robustness in the face of disconnection / reconnection of MIDI devices.
* Integer timestamps everywhere, and in nanoseconds. Additionnally, it is now possible to choose different timestamping methods (e.g. relative, absolute monotonic clock...).
* Experimental API to allow to poll manually in ALSA (Sequencer and Raw), in order to give more control 
  to the user and enable processing events on any kind of Linux event-loop.
* Increase the checks done by the MIDI parser.
* Internally it's pretty much a complete rewrite. Standard threading primitives are now used, as well as modern Linux facilities for polling control (eventfd, timerfd).
  Most of the code has been refactored.

### Since v3
* Allow to pass `span` when available (C++20) or `(uint8_t* bytes, std::size_t size)` pairs whenever possible to reduce copying.

### Since v1
* The library can be used header-only, [as explained in the docs](docs/header-only.md)
* Callbacks are passed by `std::function` and generally simplified.
* Ability to use `boost::small_vector` to pass midi bytes instead of `std::vector` to reduce allocations.
* Less indirections, virtuals and memory allocations.
* Simplify usage of some functions, use C++ return style everywhere.
* Use of standard C++ `snake_case`.
* Simplification of exceptions.
* Passes clean through clang-tidy, clang analyzer, GCC -Wall -Wextra, ASAN, UBSAN etc etc.
* Support chunking of output data (only supported on raw ALSA backend so far).

#### New & improved backends
* JACK support on Windows.
* JACK support through weakjack to allow runtime loading of JACK.
* UWP MIDI support on Windows
* Emscripten support to run on a web browser with WebMIDI.
* Raw ALSA support in addition to the existing ALSA sequencer support.

## Roadmap
* Migrate to std::expected instead of exceptions for error handling.
* Finish MIDI 2 implementations, provide helpers, etc.
* ~More tests and compliance checks~
* ~Work even more towards this library being a zero-cost abstraction on top of native MIDI APIs~
* ~Rethink some design issues with the original RtMidi, for instance the way port numbers work is not reliable~
* ~Refactor duplicated code across backends~

# They use this library

* [ossia.io](https://ossia.io): libremidi is used for every MIDI operation.
