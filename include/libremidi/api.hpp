#pragma once
#include <libremidi/config.hpp>

#include <string_view>
#include <vector>

namespace libremidi
{
//! MIDI API specifier arguments.
//! To get information on which feature is supported by each back-end, check their backend file
//! in e.g. backends/winmm.hpp, etc.
enum class API
{
  UNSPECIFIED, /*!< Search for a working compiled API. */

  // MIDI 1.0 APIs
  COREMIDI,    /*!< macOS CoreMidi API. */
  ALSA_SEQ,    /*!< Linux ALSA Sequencer API. */
  ALSA_RAW,    /*!< Linux Raw ALSA API. */
  JACK_MIDI,   /*!< JACK Low-Latency MIDI Server API. */
  WINDOWS_MM,  /*!< Microsoft Multimedia MIDI API. */
  WINDOWS_UWP, /*!< Microsoft WinRT MIDI API. */
  WEBMIDI,     /*!< Web MIDI API through Emscripten */
  PIPEWIRE,    /*!< PipeWire */

  // MIDI 2.0 APIs
  ALSA_RAW_UMP,          /*!< Raw ALSA API for MIDI 2.0 */
  ALSA_SEQ_UMP,          /*!< Linux ALSA Sequencer API for MIDI 2.0 */
  COREMIDI_UMP,          /*!< macOS CoreMidi API for MIDI 2.0. Requires macOS 11+ */
  WINDOWS_MIDI_SERVICES, /*!< Windows API for MIDI 2.0. Requires Windows 11 */

  DUMMY /*!< A compilable but non-functional API. */
};

/**
 * \brief A function to determine the available compiled MIDI 1.0 APIs.

  The values returned in the std::vector can be compared against
  the enumerated list values.  Note that there can be more than one
  API compiled for certain operating systems.
*/
LIBREMIDI_EXPORT std::vector<libremidi::API> available_apis() noexcept;

/**
 * \brief A function to determine the available compiled MIDI 2.0 APIs.

  The values returned in the std::vector can be compared against
  the enumerated list values.  Note that there can be more than one
  API compiled for certain operating systems.
*/
LIBREMIDI_EXPORT std::vector<libremidi::API> available_ump_apis() noexcept;

//! A static function to determine the current version.
LIBREMIDI_EXPORT std::string_view get_version() noexcept;

//! Map from and to API names
LIBREMIDI_EXPORT std::string_view get_api_name(libremidi::API api);
//! Map from and to API names
LIBREMIDI_EXPORT std::string_view get_api_display_name(libremidi::API api);
//! Look-up an API through its name
LIBREMIDI_EXPORT libremidi::API get_compiled_api_by_name(std::string_view api);

namespace midi1
{
//! Returns the default MIDI 1.0 backend to use for the target OS.
inline constexpr libremidi::API default_api() noexcept
{
#if defined(__APPLE__)
  return API::COREMIDI;
#elif defined(_WIN32)
  return API::WINDOWS_MM;
#elif defined(__linux__)
  return API::ALSA_SEQ;
#elif defined(__emscripten__)
  return API::EMSCRIPTEN_WEBMIDI;
#else
  return API::DUMMY;
#endif
}
}

namespace midi2
{
//! Returns the default MIDI 2.0 backend to use for the target OS.
inline constexpr libremidi::API default_api() noexcept
{
#if defined(__APPLE__)
  return API::COREMIDI_UMP;
#elif defined(_WIN32)
  return API::WINDOWS_MIDI_SERVICES;
#elif defined(__linux__)
  return API::ALSA_SEQ_UMP;
#elif defined(__emscripten__)
  return API::DUMMY;
#else
  return API::DUMMY;
#endif
}
}
}
