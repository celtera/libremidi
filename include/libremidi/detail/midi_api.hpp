#pragma once
#include <libremidi/api.hpp>
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>
#include <libremidi/observer_configuration.hpp>

#include <iostream>
#include <string_view>

namespace libremidi
{
struct error_handler
{
  //! Error reporting function for libremidi classes. Throws.
  void error(auto& configuration, std::string_view errorString) const
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
#if defined(__LIBREMIDI_DEBUG__)
      std::cerr << '\n' << errorString << "\n\n";
#endif
    }
  }

  //! Warning reporting function for libremidi classes.
  void warning(auto& configuration, std::string_view errorString) const
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

#if defined(__LIBREMIDI_DEBUG__)
    std::cerr << '\n' << errorString << "\n\n";
#endif
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

  [[nodiscard]] virtual std::error_code open_virtual_port(std::string_view)
  {
    return std::make_error_code(std::errc::function_not_supported);
  }
  virtual std::error_code set_client_name(std::string_view)
  {
    return std::make_error_code(std::errc::function_not_supported);
  }
  virtual std::error_code set_port_name(std::string_view)
  {
    return std::make_error_code(std::errc::function_not_supported);
  }

  virtual std::error_code close_port() = 0;

  bool is_port_open() const noexcept { return bool(port_open_); }
  bool is_port_connected() const noexcept { return bool(connected_); }

protected:
  friend class midi_in;
  friend class midi_out;
  bool port_open_{};
  bool connected_{};
};
}
