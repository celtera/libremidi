#pragma once
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/input_configuration.hpp>

namespace libremidi
{

class midi_in_api : public midi_api
{
  friend struct midi_stream_decoder;

public:
  explicit midi_in_api() { }

  ~midi_in_api() override = default;

  midi_in_api(const midi_in_api&) = delete;
  midi_in_api(midi_in_api&&) = delete;
  midi_in_api& operator=(const midi_in_api&) = delete;
  midi_in_api& operator=(midi_in_api&&) = delete;

protected:
  libremidi::message message{};
  bool continueSysex{false};
  bool firstMessage{true};
};

template <typename T>
class midi_in_default
    : public midi_in_api
    , public error_handler
{
  using midi_in_api::midi_in_api;
  void open_virtual_port(std::string_view) override
  {
    using namespace std::literals;
    warning(T::backend + " in: open_virtual_port unsupported"s);
  }
  void set_client_name(std::string_view) override
  {
    using namespace std::literals;
    warning(T::backend + " in: set_client_name unsupported"s);
  }
  void set_port_name(std::string_view) override
  {
    using namespace std::literals;
    warning(T::backend + " in: set_port_name unsupported"s);
  }
};

}
