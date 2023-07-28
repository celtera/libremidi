#pragma once
#include <libremidi/config.hpp>

#include <string_view>
#include <vector>

namespace libremidi
{
//! MIDI API specifier arguments.
enum class API
{
  UNSPECIFIED, /*!< Search for a working compiled API. */

  // MIDI 1.0 APIs
  MACOSX_CORE, /*!< Macintosh OS-X Core Midi API. */
  LINUX_ALSA,  /*!< The Advanced Linux Sound Architecture API. */
  LINUX_ALSA_SEQ = LINUX_ALSA,
  LINUX_ALSA_RAW,     /*!< Raw ALSA API. */
  UNIX_JACK,          /*!< The JACK Low-Latency MIDI Server API. */
  WINDOWS_MM,         /*!< The Microsoft Multimedia MIDI API. */
  WINDOWS_UWP,        /*!< The Microsoft WinRT MIDI API. */
  EMSCRIPTEN_WEBMIDI, /*!< Web MIDI API through Emscripten */

  // MIDI 2.0 APIs
  ALSA_RAW_UMP, /*!< Raw ALSA API for MIDI 2.0 */
  COREMIDI_UMP,
  WINDOWS_MIDI_SERVICES,

  DUMMY /*!< A compilable but non-functional API. */
};

/**
 * \brief A function to determine the available compiled MIDI APIs.

  The values returned in the std::vector can be compared against
  the enumerated list values.  Note that there can be more than one
  API compiled for certain operating systems.
*/
LIBREMIDI_EXPORT std::vector<libremidi::API> available_apis() noexcept;
LIBREMIDI_EXPORT std::vector<libremidi::API> available_ump_apis() noexcept;

//! A static function to determine the current version.
LIBREMIDI_EXPORT std::string_view get_version() noexcept;

//! Map from and to API names
LIBREMIDI_EXPORT std::string_view get_api_name(libremidi::API api);
LIBREMIDI_EXPORT std::string_view get_api_display_name(libremidi::API api);
LIBREMIDI_EXPORT libremidi::API get_compiled_api_by_name(std::string_view api);

namespace midi1
{
/**
 * @brief Returns the default backend to use for the target OS.
 *
 * e.g. the one that uses the "recommended" system MIDI API.
 */
inline constexpr libremidi::API default_api() noexcept
{
#if defined(__APPLE__)
  return API::MACOSX_CORE;
#elif defined(_WIN32)
  return API::WINDOWS_MM;
#elif defined(__linux__)
  return API::LINUX_ALSA_SEQ;
#elif defined(__emscripten__)
  return API::EMSCRIPTEN_WEBMIDI;
#else
  return API::DUMMY;
#endif
}
}

namespace midi2
{
inline constexpr libremidi::API default_api() noexcept
{
#if defined(__APPLE__)
  return API::COREMIDI_UMP;
#elif defined(_WIN32)
  return API::WINDOWS_MIDI_SERVICES;
#elif defined(__linux__)
  return API::ALSA_RAW_UMP;
#elif defined(__emscripten__)
  return API::DUMMY;
#else
  return API::DUMMY;
#endif
}
}
}
