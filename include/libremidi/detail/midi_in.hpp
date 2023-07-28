#pragma once
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/input_configuration.hpp>

namespace libremidi
{
class midi_in_api : public midi_api
{
  friend struct midi_stream_decoder;

public:
  midi_in_api() = default;
  ~midi_in_api() override = default;
  midi_in_api(const midi_in_api&) = delete;
  midi_in_api(midi_in_api&&) = delete;
  midi_in_api& operator=(const midi_in_api&) = delete;
  midi_in_api& operator=(midi_in_api&&) = delete;
};
namespace midi1 {
class in_api : public midi_in_api
{
  friend struct midi_stream_decoder;

public:
  using midi_in_api::midi_in_api;

protected:
  libremidi::message message{};
  bool continueSysex{false};
  bool firstMessage{true};
};
}

namespace midi2 {
class in_api : public midi_in_api
{
  friend struct midi_stream_decoder;

public:
  using midi_in_api::midi_in_api;

protected:
  libremidi::ump message{};
  bool continueSysex{false};
  bool firstMessage{true};
};
}

template <typename T, typename Arg>
std::unique_ptr<midi_in_api> make(libremidi::input_configuration&& conf, Arg&& arg)
{
  return std::make_unique<T>(std::move(conf), std::move(arg));
}

template <typename T, typename Arg>
std::unique_ptr<midi_in_api> make(libremidi::ump_input_configuration&& conf, Arg&& arg)
{
  return std::make_unique<T>(std::move(conf), std::move(arg));
}
}
