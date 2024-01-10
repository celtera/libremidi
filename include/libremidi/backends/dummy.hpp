#pragma once
#include <libremidi/configurations.hpp>
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_out.hpp>
#include <libremidi/detail/observer.hpp>

namespace libremidi
{
class observer_dummy : public observer_api
{
public:
  explicit observer_dummy(const auto& /*configuration*/, dummy_configuration) { }

  ~observer_dummy() { }
  libremidi::API get_current_api() const noexcept override { return libremidi::API::DUMMY; }
  std::vector<libremidi::input_port> get_input_ports() const noexcept override { return {}; }
  std::vector<libremidi::output_port> get_output_ports() const noexcept override { return {}; }
};

class midi_in_dummy final
    : public midi1::in_api
    , public error_handler
{
public:
  explicit midi_in_dummy(const auto& configuration, dummy_configuration)
  {
    warning(configuration, "midi_in_dummy: This class provides no functionality.");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::DUMMY; }
  bool open_port(const input_port& /*pt*/, std::string_view /*local_port_name*/) override
  {
    return true;
  }
  bool open_virtual_port(std::string_view /*portName*/) override { return true; }
  void close_port() override { }
  void set_client_name(std::string_view /*clientName*/) override { }
  void set_port_name(std::string_view /*portName*/) override { }
};

class midi_out_dummy final
    : public midi1::out_api
    , public error_handler
{
public:
  explicit midi_out_dummy(const auto& configuration, dummy_configuration)
  {
    warning(configuration, "midi_out_dummy: This class provides no functionality.");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::DUMMY; }
  bool open_port(const output_port& /*pt*/, std::string_view /*local_port_name*/) override
  {
    return true;
  }
  bool open_virtual_port(std::string_view /*portName*/) override { return true; }
  void close_port() override { }
  void set_client_name(std::string_view /*clientName*/) override { }
  void set_port_name(std::string_view /*portName*/) override { }
  void send_message(const unsigned char* /*message*/, size_t /*size*/) override { }
};

struct dummy_backend
{
  using midi_in = midi_in_dummy;
  using midi_out = midi_out_dummy;
  using midi_observer = observer_dummy;
  using midi_in_configuration = dummy_configuration;
  using midi_out_configuration = dummy_configuration;
  using midi_observer_configuration = dummy_configuration;
  static const constexpr auto API = libremidi::API::DUMMY;
  static const constexpr auto name = "dummy";
  static const constexpr auto display_name = "Dummy";

  static constexpr inline bool available() noexcept { return true; }
};
}
