#pragma once
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/detail/midi_queue.hpp>

namespace libremidi
{

class midi_in_api : public midi_api
{
public:
  explicit midi_in_api(void* data, unsigned int queueSizeLimit)
  {
    inputData_.apiData = data;
    // Allocate the MIDI queue.
    inputData_.queue.ringSize = queueSizeLimit;
    if (inputData_.queue.ringSize > 0)
    {
      inputData_.queue.ring = std::make_unique<libremidi::message[]>(inputData_.queue.ringSize);
    }
  }
  ~midi_in_api() override = default;

  midi_in_api(const midi_in_api&) = delete;
  midi_in_api(midi_in_api&&) = delete;
  midi_in_api& operator=(const midi_in_api&) = delete;
  midi_in_api& operator=(midi_in_api&&) = delete;

  virtual void ignore_types(bool midiSysex, bool midiTime, bool midiSense)
  {
    inputData_.ignoreFlags = 0;
    if (midiSysex)
    {
      inputData_.ignoreFlags = 0x01;
    }
    if (midiTime)
    {
      inputData_.ignoreFlags |= 0x02;
    }
    if (midiSense)
    {
      inputData_.ignoreFlags |= 0x04;
    }
  }

  void set_callback(midi_in::message_callback callback)
  {
    inputData_.userCallback = std::move(callback);
  }

  void cancel_callback() { inputData_.userCallback = midi_in::message_callback{}; }

  message get_message()
  {
    if (inputData_.userCallback)
    {
      warning(
          "midi_in_api::getNextMessage: a user callback is currently set for "
          "this port.");
      return {};
    }

    message m;
    if (inputData_.queue.pop(m))
    {
      return m;
    }
    return {};
  }

  bool get_message(message& m)
  {
    if (inputData_.userCallback)
    {
      warning(
          "midi_in_api::get_message: a user callback is currently set for "
          "this port.");
      return {};
    }

    return inputData_.queue.pop(m);
  }

  // The in_data structure is used to pass private class data to
  // the MIDI input handling function or thread.
  struct in_data
  {
    midi_queue queue{};
    libremidi::message message{};
    unsigned char ignoreFlags{7};
    bool firstMessage{true};
    void* apiData{};
    midi_in::message_callback userCallback{};
    bool continueSysex{false};

    void on_message_received(libremidi::message&& message)
    {
      if (userCallback)
      {
        userCallback(std::move(message));
      }
      else
      {
        // As long as we haven't reached our queue size limit, push the
        // message.
        if (!queue.push(std::move(message)))
        {
#if defined(__LIBREMIDI_DEBUG__)
          std::cerr << "\nmidi_in: message queue limit reached!!\n\n";
#endif
        }
      }
      message.bytes.clear();
    }
  };

protected:
  in_data inputData_{};
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
