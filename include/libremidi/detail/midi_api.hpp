#pragma once
#include <libremidi/libremidi.hpp>

#include <iostream>
#include <string_view>

namespace libremidi
{
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

  void set_error_callback(midi_error_callback errorCallback) noexcept
  {
    errorCallback_ = std::move(errorCallback);
  }

  //! Error reporting function for libremidi classes. Throws.
  template <typename Error_T>
  void error(std::string_view errorString) const
  {
    if (errorCallback_)
    {
      if (firstErrorOccurred_)
      {
        return;
      }

      firstErrorOccurred_ = true;
      errorCallback_(Error_T::code, errorString);
      firstErrorOccurred_ = false;
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
  void warning(std::string_view errorString) const
  {
    if (errorCallback_)
    {
      if (firstErrorOccurred_)
      {
        return;
      }

      firstErrorOccurred_ = true;
      errorCallback_(midi_error::WARNING, errorString);
      firstErrorOccurred_ = false;
      return;
    }

#if defined(__LIBREMIDI_DEBUG__)
    std::cerr << '\n' << errorString << "\n\n";
#endif
  }

protected:
  midi_error_callback errorCallback_;
  bool connected_{};
  mutable bool firstErrorOccurred_{};
};

template <typename T, auto func>
using unique_handle = std::unique_ptr<T, decltype([](T* x) { func(x); })>;

}
