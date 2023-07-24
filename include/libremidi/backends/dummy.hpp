#pragma once
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_out.hpp>
#include <libremidi/detail/observer.hpp>
#include <libremidi/libremidi.hpp>

namespace libremidi
{
struct dummy_configuration
{
};

class observer_dummy final : public observer_api
{
public:
  explicit observer_dummy(const observer_configuration& configuration, dummy_configuration)
  {
  }

  ~observer_dummy() { }
};

class midi_in_dummy final
    : public midi_in_api
    , public error_handler
{
public:
  explicit midi_in_dummy(const input_configuration& configuration, dummy_configuration)
  {
    warning(configuration, "midi_in_dummy: This class provides no functionality.");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::DUMMY; }

  void open_port(unsigned int /*portNumber*/, std::string_view /*portName*/) override { }

  void open_virtual_port(std::string_view /*portName*/) override { }

  void close_port() override { }

  void set_client_name(std::string_view /*clientName*/) override { }

  void set_port_name(std::string_view /*portName*/) override { }

  unsigned int get_port_count() const override { return 0; }

  std::string get_port_name(unsigned int /*portNumber*/) const override
  {
    using namespace std::literals;
    return ""s;
  }
};

class midi_out_dummy final
    : public midi_out_api
    , public error_handler
{
public:
  explicit midi_out_dummy(const output_configuration& configuration, dummy_configuration)
  {
    warning(configuration, "midi_out_dummy: This class provides no functionality.");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::DUMMY; }

  void open_port(unsigned int /*portNumber*/, std::string_view /*portName*/) override { }
  void open_virtual_port(std::string_view /*portName*/) override { }
  void close_port() override { }
  void set_client_name(std::string_view /*clientName*/) override { }
  void set_port_name(std::string_view /*portName*/) override { }
  unsigned int get_port_count() const override { return 0; }
  std::string get_port_name(unsigned int /*portNumber*/) const override
  {
    using namespace std::literals;
    return ""s;
  }
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
};
}
