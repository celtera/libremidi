#pragma once
#include <rtmidi17/detail/midi_api.hpp>
#include <rtmidi17/rtmidi17.hpp>

namespace rtmidi
{
class midi_in_dummy final : public midi_in_api
{
public:
  midi_in_dummy(const std::string& /*clientName*/, unsigned int queueSizeLimit)
      : midi_in_api(queueSizeLimit)
  {
    warning("midi_in_dummy: This class provides no functionality.");
  }

  rtmidi::API get_current_api() const noexcept override
  {
    return rtmidi::API::DUMMY;
  }

  void open_port(unsigned int /*portNumber*/, const std::string& /*portName*/) override
  {
  }

  void open_virtual_port(const std::string& /*portName*/) override
  {
  }

  void close_port() override
  {
  }

  void set_client_name(const std::string& /*clientName*/) override
  {
  }

  void set_port_name(const std::string& /*portName*/) override
  {
  }

  unsigned int get_port_count() override
  {
    return 0;
  }

  std::string get_port_name(unsigned int /*portNumber*/) override
  {
    using namespace std::literals;
    return ""s;
  }
};

class midi_out_dummy final : public midi_out_api
{
public:
  explicit midi_out_dummy(const std::string& /*clientName*/)
  {
    warning("midi_out_dummy: This class provides no functionality.");
  }

  rtmidi::API get_current_api() const noexcept override
  {
    return rtmidi::API::DUMMY;
  }

  void open_port(unsigned int /*portNumber*/, const std::string& /*portName*/) override
  {
  }
  void open_virtual_port(const std::string& /*portName*/) override
  {
  }
  void close_port() override
  {
  }
  void set_client_name(const std::string& /*clientName*/) override
  {
  }
  void set_port_name(const std::string& /*portName*/) override
  {
  }
  unsigned int get_port_count() override
  {
    return 0;
  }
  std::string get_port_name(unsigned int /*portNumber*/) override
  {
    using namespace std::literals;
    return ""s;
  }
  void send_message(const unsigned char* /*message*/, size_t /*size*/) override
  {
  }
};
}
