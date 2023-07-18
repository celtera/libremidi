#pragma once
#include <libremidi/detail/midi_api.hpp>

namespace libremidi
{

class midi_in_api : public midi_api
{
  friend struct midi_stream_decoder;

public:
  explicit midi_in_api() { cancel_callback(); }

  ~midi_in_api() override = default;

  midi_in_api(const midi_in_api&) = delete;
  midi_in_api(midi_in_api&&) = delete;
  midi_in_api& operator=(const midi_in_api&) = delete;
  midi_in_api& operator=(midi_in_api&&) = delete;

  void ignore_types(bool midiSysex, bool midiTime, bool midiSense)
  {
    this->ignoreFlags = 0;
    if (midiSysex)
    {
      this->ignoreFlags = 0x01;
    }
    if (midiTime)
    {
      this->ignoreFlags |= 0x02;
    }
    if (midiSense)
    {
      this->ignoreFlags |= 0x04;
    }
  }

  // FIXME not thread safe
  void set_callback(midi_in::message_callback callback)
  {
    if (!callback)
      cancel_callback();
    else
      this->userCallback = std::move(callback);
  }

  void cancel_callback()
  {
    this->userCallback = [](libremidi::message&& m) {};
  }

protected:
  midi_in::message_callback userCallback{};
  libremidi::message message{};
  bool continueSysex{false};
  unsigned char ignoreFlags{7};
  bool firstMessage{true};

  void on_message_received(libremidi::message&& message)
  {
    userCallback(std::move(message));
    message.bytes.clear();
  }
};

template <typename T>
class midi_in_default : public midi_in_api
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
