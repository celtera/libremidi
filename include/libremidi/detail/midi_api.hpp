#pragma once
#include <libremidi/api.hpp>
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>

#include <iostream>
#include <string_view>

namespace libremidi
{
struct error_handler
{
  //! Error reporting function for libremidi classes. Throws.
  template <typename Error_T>
  void error(auto& configuration, std::string_view errorString) const
  {
    if (configuration.on_error)
    {
      if (first_error)
        return;

      first_error = true;
      configuration.on_error(Error_T::code, errorString);
      first_error = false;
    }
    else
    {
#if defined(__LIBREMIDI_DEBUG__)
      std::cerr << '\n' << errorString << "\n\n";
#endif
      throw Error_T{errorString.data()};
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
      configuration.on_warning(midi_error::WARNING, errorString);
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
  virtual void open_port(unsigned int portNumber, std::string_view portName) = 0;
  virtual void open_virtual_port(std::string_view) = 0;
  virtual void close_port() = 0;
  virtual void set_client_name(std::string_view) = 0;
  virtual void set_port_name(std::string_view) = 0;

  [[nodiscard]] virtual unsigned int get_port_count() const = 0;

  [[nodiscard]] virtual std::string get_port_name(unsigned int portNumber) const = 0;

  bool is_port_open() const noexcept { return bool(connected_); }

protected:
  bool connected_{};
};

template <auto func>
struct deleter
{
  template <typename U>
  void operator()(U* x)
  {
    func(x);
  }
};

template <typename T, auto func>
using unique_handle = std::unique_ptr<T, deleter<func>>;
}
