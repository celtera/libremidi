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

}
