#pragma once
#include <libremidi/detail/midi_api.hpp>

namespace libremidi
{
struct input_configuration
{
  midi_in::message_callback on_message;

  midi_error_callback on_error{};
  midi_error_callback on_warning{};

  enum timestamp_mode
  {
    NoTimestamp,
    Relative, // Revert to old behaviour
    Absolute  // In nanoseconds, as per std::high_resolution_clock::now()
  };
  uint32_t ignore_sysex : 1 = false;
  uint32_t ignore_timing : 1 = false;
  uint32_t ignore_sensing : 1 = false;
  uint32_t timestamps : 2 = timestamp_mode::Relative;
};

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
    // FIXME not thread safe
    configuration.ignore_sysex = midiSysex;
    configuration.ignore_timing = midiTime;
    configuration.ignore_sensing = midiSense;
  }

  void set_callback(midi_in::message_callback callback)
  {
    // FIXME not thread safe
    if (!callback)
      cancel_callback();
    else
      configuration.on_message = std::move(callback);
  }

  void cancel_callback()
  {
    configuration.on_message = [](libremidi::message&& m) {};
  }

protected:
  input_configuration configuration;

  libremidi::message message{};
  bool continueSysex{false};
  bool firstMessage{true};

  void on_message_received(libremidi::message&& message)
  {
    configuration.on_message(std::move(message));
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
