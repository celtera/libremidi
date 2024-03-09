#pragma once
#include <libremidi/api.hpp>
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>
#include <libremidi/observer_configuration.hpp>

#include <source_location>
#include <string_view>

namespace libremidi
{
struct error_handler
{
  //! Error reporting function for libremidi classes. Throws.
  void error(
      auto& configuration, std::string_view errorString,
      const std::source_location location = std::source_location::current()) const
  {
    if (configuration.on_error)
    {
      if (first_error)
        return;

      first_error = true;
      configuration.on_error(errorString);
      first_error = false;
    }
    else
    {
      LIBREMIDI_LOG(errorString);
    }
  }

  //! Warning reporting function for libremidi classes.
  void warning(
      auto& configuration, std::string_view errorString,
      const std::source_location location = std::source_location::current()) const
  {
    if (configuration.on_warning)
    {
      if (first_warning)
        return;

      first_warning = true;
      configuration.on_warning(errorString);
      first_warning = false;
      return;
    }

    LIBREMIDI_LOG(errorString);
  }

  // To prevent infinite error loops
  mutable bool first_error{};
  mutable bool first_warning{};
};

class midi_api
{
public:
  midi_api() = default;
  virtual ~midi_api() = default;
  midi_api(const midi_api&) = delete;
  midi_api(midi_api&&) = delete;
  midi_api& operator=(const midi_api&) = delete;
  midi_api& operator=(midi_api&&) = delete;

  [[nodiscard]] virtual libremidi::API get_current_api() const noexcept = 0;

  [[nodiscard]] virtual stdx::error open_virtual_port(std::string_view)
  {
    return std::errc::function_not_supported;
  }
  virtual stdx::error set_client_name(std::string_view)
  {
    return std::errc::function_not_supported;
  }
  virtual stdx::error set_port_name(std::string_view)
  {
    return std::errc::function_not_supported;
  }

  virtual stdx::error close_port() = 0;

  bool is_port_open() const noexcept { return bool(port_open_); }
  bool is_port_connected() const noexcept { return bool(connected_); }

protected:
  friend class midi_in;
  friend class midi_out;
  bool port_open_{};
  bool connected_{};
};
}
