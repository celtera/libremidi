#pragma once
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/libremidi.hpp>
#include <libremidi/output_configuration.hpp>

#include <string_view>

namespace libremidi
{

class midi_out_api : public midi_api
{
public:
  virtual void send_message(const unsigned char* message, size_t size) = 0;
};

template <typename T>
class midi_out_default
    : public midi_out_api
    , public error_handler
{
  using midi_out_api::midi_out_api;
  void open_virtual_port(std::string_view) override
  {
    using namespace std::literals;
    warning(T::backend + " out: open_virtual_port unsupported"s);
  }
  void set_client_name(std::string_view) override
  {
    using namespace std::literals;
    warning(T::backend + " out: set_client_name unsupported"s);
  }
  void set_port_name(std::string_view) override
  {
    using namespace std::literals;
    warning(T::backend + " out: set_port_name unsupported"s);
  }
};
}
