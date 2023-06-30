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

#define LIBREMIDI_VERSION "3.0.0"

#if defined(LIBREMIDI_USE_BOOST)
  #if ! __has_include(<boost/container/small_vector.hpp>)
    #error "Boost was used for building libremidi but is not found when using it. Add Boost to your include paths."
  #endif

  #if defined(LIBREMIDI_NO_BOOST)
    #error "Boost was used for building libremidi but LIBREMIDI_NO_BOOST is defined."
  #endif
#endif

#if __has_include(<boost/container/small_vector.hpp>) && !defined(LIBREMIDI_NO_BOOST)
#include <boost/container/small_vector.hpp>
namespace libremidi
{
using midi_bytes = boost::container::small_vector<unsigned char, 4>;
}
#else
namespace libremidi
{
using midi_bytes = std::vector<unsigned char>;
}
#endif

#if defined(LIBREMIDI_HEADER_ONLY)
  #define LIBREMIDI_INLINE inline
#else
  #define LIBREMIDI_INLINE
#endif
