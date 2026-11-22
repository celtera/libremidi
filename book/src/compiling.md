# Compiling the library

libremidi uses CMake as build system.
The easiest way to compile the library is to take inspiration from the CI scripts as they will install the required dependencies.

Some backends require an up-to-date C++20 compiler: JACK and PipeWire, as we leverage the C++20 semaphore support.

Compiling the library and examples is as simple as:

```bash
$ cmake -Wno-dev \
  -S path/to/libremidi \
  -B build_folder \
  -DLIBREMIDI_EXAMPLES=1 

$ cmake --build build_folder
```

libremidi is also available on [vcpkg](https://vcpkg.link/ports/libremidi) and [Nixpkgs](https://mynixos.com/nixpkgs/package/libremidi).

## On Linux & BSD

Note that the ALSA and PipeWire back-end rely on timerfd and eventfd which 
may not be available on very, very, very old Linux kernels (< 3.x) or some BSD kernels.

Note that the ALSA Raw back-end also needs udev access to support hotplug for USB peripherals.

- Debian & Ubuntu packages for all the back-ends: 
```
libasound-dev
libjack-jackd2-dev
libudev-dev
libpipewire-0.3-dev
```

- ArchLinux packages for all the back-ends: 
```
alsa-lib
jack2
libpipewire
systemd-libs
```

## On Windows
Note that when targetting Windows Store, WinMM APIs are not available, only UWP ones.

Both MSVC and MSYS2 are supported (on all MSYS2 toolchains).

Note that for MIDI 2 support it is currently necessary for the end-user to install the [Windows MIDI Services](https://github.com/microsoft/MIDI/releases) on their computer. As of version 5, libremidi has been tested on Windows 10 and 11 with the DP9 NAMM Preview 4 release.

### Cross-compiling for Windows XP (32-bit)

The [Windows XP workflow](https://github.com/celtera/libremidi/blob/master/.github/workflows/windows-xp.yml) cross-compiles the WinMM backend on Linux with [LLVM-MinGW-XP](https://github.com/mon/llvm-mingw-xp). To reproduce it, unpack the pinned MSVCRT release, add its `bin` directory to `PATH`, and use the workflow's CMake command with `--toolchain` pointing to the archive's `toolchain-files/cmake/i686-mingw32-clang.cmake`. WinUWP and Windows MIDI Services are not available on XP.

The tested baseline for the `midiprobe-windows-xp-x86` artifact is **32-bit Windows XP SP3 on a CPU with SSE2**. The hosted executable enumerated ports on XP SP3 with SSE2; it did not run with SSE2 disabled. Earlier service packs and pre-SSE2 CPUs are not supported by this build. To enumerate ports, download the artifact from a workflow run on `master` or on a release tag (pull request runs upload an artifact of the same name built from unreviewed code) and run `midiprobe.exe` from a command prompt; no separate C++ runtime is needed.

Before upload, CI checks two executables: `midiprobe.exe`, which only enumerates ports, and the `echo` example, which also opens, reads and writes them. For each it checks the x86 PE32 format, the subsystem version (5.1), the WinMM dependency, and every static import against [a list of reviewed DLL/symbol pairs](https://github.com/celtera/libremidi/blob/master/.github/scripts/xp-sp3-x86-imports.txt). That list is a **snapshot of the imports of these two executables**, not a catalog of the XP API: any new import, even a harmless one brought by a toolchain update or an edit to an example, fails the workflow until it is reviewed. The `midiprobe.exe` pairs were checked against the exports of the corresponding 32-bit XP SP3 system DLLs; the additional `echo` pairs only against the minimum supported client in Microsoft's documentation, as the comments in the list say. Only `midiprobe.exe` has been run on XP. To review a new pair, list the imports with `llvm-readobj --coff-imports <exe>` and verify the exact symbol with `llvm-readobj --coff-exports` on the DLL from a licensed XP SP3 installation, or failing that in Microsoft's documentation, then add it to the list with a comment naming the source. Do not regenerate the list solely from a new build. Ordinal and delay imports are not approved. This static check does not exercise the executables or cover runtime lookups such as `GetProcAddress`.

## On macOS & iOS
Note that for MIDI 2 support, the application needs to target at least macOS 11.0.
e.g. `-DCMAKE_OSX_DEPLOYMENT_TARGET=11` or more recent needs to be passed to CMake.

## Advanced features

- For MIDI 1: ability to set a fixed message size upper-bound for zero-allocation scenarios, with `-DLIBREMIDI_SLIM_MESSAGE=<NBytes>` (in CMake or directly to the compiler)
