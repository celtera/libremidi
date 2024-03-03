#pragma once

#if defined(MSC_VER)
  #define NOMINMAX 1
  #define WIN32_LEAN_AND_MEAN
#endif
#include <algorithm>
#include <cinttypes>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(LIBREMIDI_EXPORTS)
  #if defined(_MSC_VER)
    #define LIBREMIDI_EXPORT __declspec(dllexport)
  #elif defined(__GNUC__) || defined(__clang__)
    #define LIBREMIDI_EXPORT __attribute__((visibility("default")))
  #endif
#else
  #define LIBREMIDI_EXPORT
#endif

#define LIBREMIDI_VERSION "4.5.0"

#if defined(LIBREMIDI_USE_BOOST)
  #if !__has_include(<boost/container/small_vector.hpp>)
    #error \
        "Boost was used for building libremidi but is not found when using it. Add Boost to your include paths."
  #endif

  #if defined(LIBREMIDI_NO_BOOST)
    #error "Boost was used for building libremidi but LIBREMIDI_NO_BOOST is defined."
  #endif
#endif

#if __has_include(<boost/container/small_vector.hpp>) && !defined(LIBREMIDI_NO_BOOST)

  #if LIBREMIDI_SLIM_MESSAGE > 0
    #include <boost/container/static_vector.hpp>
namespace libremidi
{
using midi_bytes = boost::container::static_vector<unsigned char, LIBREMIDI_SLIM_MESSAGE>;
}
  #else
    #include <boost/container/small_vector.hpp>
namespace libremidi
{
static constexpr int small_vector_minimum_size
    = sizeof(boost::container::small_vector<unsigned char, 1>);
using midi_bytes = boost::container::small_vector<unsigned char, small_vector_minimum_size>;
}
  #endif
#else
namespace libremidi
{
using midi_bytes = std::vector<unsigned char>;
}
#endif

#if __has_include(<midi/universal_packet.h>) && defined(LIBREMIDI_USE_NI_MIDI2)
  #define LIBREMIDI_NI_MIDI2_COMPAT 1
#endif

#if defined(LIBREMIDI_HEADER_ONLY)
  #define LIBREMIDI_INLINE inline
#else
  #define LIBREMIDI_INLINE
#endif
