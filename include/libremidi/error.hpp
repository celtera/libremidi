#pragma once
#include <libremidi/config.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <libremidi/system_error2.hpp>
#pragma GCC diagnostic pop

#include <functional>
#include <iostream>
#include <source_location>
#include <string_view>

namespace libremidi
{
inline auto from_errc(int ret) noexcept
{
  return std::make_error_code(static_cast<std::errc>(ret));
}

/*! \brief Error callback function
    \param type Type of error.
    \param errorText Error description.

    Note that class behaviour is undefined after a critical error (not
    a warning) is reported.
 */
using midi_error_callback = std::function<void(std::string_view errorText, const std::source_location&)>;
using midi_warning_callback = std::function<void(std::string_view errorText, const std::source_location&)>;
}

#if !defined(LIBREMIDI_LOG)
  #if defined(__LIBREMIDI_DEBUG__)
    #define LIBREMIDI_LOG(...) \
      do                       \
      {                        \
      } while (0)
  #else
    #include <iostream>
    #define LIBREMIDI_LOG(...)        \
      do                              \
      {                               \
        [](auto&&... args) {          \
          (std::cerr << ... << args); \
          std::cerr << std::endl;     \
        }(__VA_ARGS__);               \
      } while (0)
  #endif
#endif
