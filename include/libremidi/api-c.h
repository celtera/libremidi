#pragma once

#if __cplusplus
extern "C" {
#endif

//! MIDI API specifier arguments.
//! To get information on which feature is supported by each back-end, check their backend file
//! in e.g. backends/winmm.hpp, etc.
typedef enum libremidi_api
{
  UNSPECIFIED = 0x0, /*!< Search for a working compiled API. */

  // MIDI 1.0 APIs
  COREMIDI = 0x1, /*!< macOS CoreMidi API. */
  ALSA_SEQ,       /*!< Linux ALSA Sequencer API. */
  ALSA_RAW,       /*!< Linux Raw ALSA API. */
  JACK_MIDI,      /*!< JACK Low-Latency MIDI Server API. */
  WINDOWS_MM,     /*!< Microsoft Multimedia MIDI API. */
  WINDOWS_UWP,    /*!< Microsoft WinRT MIDI API. */
  WEBMIDI,        /*!< Web MIDI API through Emscripten */
  PIPEWIRE,       /*!< PipeWire */
  KEYBOARD,       /*!< Computer keyboard input */

  // MIDI 2.0 APIs
  ALSA_RAW_UMP = 0x1000, /*!< Raw ALSA API for MIDI 2.0 */
  ALSA_SEQ_UMP,          /*!< Linux ALSA Sequencer API for MIDI 2.0 */
  COREMIDI_UMP,          /*!< macOS CoreMidi API for MIDI 2.0. Requires macOS 11+ */
  WINDOWS_MIDI_SERVICES, /*!< Windows API for MIDI 2.0. Requires Windows 11 */
  KEYBOARD_UMP,          /*!< Computer keyboard input */

  DUMMY = 0xFFFF /*!< A compilable but non-functional API. */
} libremidi_api;

#if __cplusplus
}
#endif
