#pragma once
#include <libremidi/config.hpp>

#include <functional>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace libremidi
{
inline auto from_errc(int ret) noexcept
{
  return std::make_error_code(static_cast<std::errc>(ret));
}

//! Defines various error types.
enum midi_error
{
  WARNING,           /*!< A non-critical error. */
  UNSPECIFIED,       /*!< The default, unspecified error type. */
  NO_DEVICES_FOUND,  /*!< No devices found on system. */
  INVALID_DEVICE,    /*!< An invalid device ID was specified. */
  MEMORY_ERROR,      /*!< An error occured during memory allocation. */
  INVALID_PARAMETER, /*!< An invalid parameter was specified to a function. */
  INVALID_USE,       /*!< The function was called incorrectly. */
  DRIVER_ERROR,      /*!< A system driver error occured. */
  SYSTEM_ERROR,      /*!< A system error occured. */
  THREAD_ERROR       /*!< A thread error occured. */
};

struct midi_error_category : public std::error_category
{
public:
  const char* name() const noexcept override { return "midi"; }

  std::string message(int code) const override
  {
    switch (code)
    {
      case midi_error::WARNING:
        return "warning";
      case midi_error::UNSPECIFIED:
        return "unspecified";
      case midi_error::NO_DEVICES_FOUND:
        return "no devices found";
      case midi_error::INVALID_DEVICE:
        return "invalid device";
      case midi_error::MEMORY_ERROR:
        return "memory error";
      case midi_error::INVALID_PARAMETER:
        return "invalid parameter";
      case midi_error::INVALID_USE:
        return "invalid use";
      case midi_error::DRIVER_ERROR:
        return "driver error";
      case midi_error::SYSTEM_ERROR:
        return "system error";
      case midi_error::THREAD_ERROR:
        return "thread error";
    }
    return "unknown";
  }
};

inline std::error_code make_error_code(midi_error e)
{
  static const midi_error_category mc;
  return std::error_code(static_cast<int>(e), mc);
}

//! Base exception class for MIDI problems
struct LIBREMIDI_EXPORT midi_exception : public std::runtime_error
{
  using std::runtime_error::runtime_error;
  ~midi_exception() override;
};

struct LIBREMIDI_EXPORT no_devices_found_error final : public midi_exception
{
  static constexpr auto code = midi_error::NO_DEVICES_FOUND;
  using midi_exception::midi_exception;
  ~no_devices_found_error() override;
};
struct LIBREMIDI_EXPORT invalid_device_error final : public midi_exception
{
  static constexpr auto code = midi_error::INVALID_DEVICE;
  using midi_exception::midi_exception;
  ~invalid_device_error() override;
};
struct LIBREMIDI_EXPORT memory_error final : public midi_exception
{
  static constexpr auto code = midi_error::MEMORY_ERROR;
  using midi_exception::midi_exception;
  ~memory_error() override;
};
struct LIBREMIDI_EXPORT invalid_parameter_error final : public midi_exception
{
  static constexpr auto code = midi_error::INVALID_PARAMETER;
  using midi_exception::midi_exception;
  ~invalid_parameter_error() override;
};
struct LIBREMIDI_EXPORT invalid_use_error final : public midi_exception
{
  static constexpr auto code = midi_error::INVALID_USE;
  using midi_exception::midi_exception;
  ~invalid_use_error() override;
};
struct LIBREMIDI_EXPORT driver_error final : public midi_exception
{
  static constexpr auto code = midi_error::DRIVER_ERROR;
  using midi_exception::midi_exception;
  ~driver_error() override;
};
struct LIBREMIDI_EXPORT system_error final : public midi_exception
{
  static constexpr auto code = midi_error::SYSTEM_ERROR;
  using midi_exception::midi_exception;
  ~system_error() override;
};
struct LIBREMIDI_EXPORT thread_error final : public midi_exception
{
  static constexpr auto code = midi_error::THREAD_ERROR;
  using midi_exception::midi_exception;
  ~thread_error() override;
};

/*! \brief Error callback function
    \param type Type of error.
    \param errorText Error description.

    Note that class behaviour is undefined after a critical error (not
    a warning) is reported.
 */
using midi_error_callback = std::function<void(midi_error type, std::string_view errorText)>;
}
