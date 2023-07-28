#pragma once

#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{

struct midi_stream_decoder
{
  uint8_t runningStatusType_{};
  message_callback& callback;
  std::vector<unsigned char> bytes;
  message msg;

  explicit midi_stream_decoder(message_callback& data)
      : callback{data}
  {
    bytes.reserve(64);
  }

  void add_bytes(unsigned char* data, std::size_t sz, int64_t nanos = 0)
  {
    msg.timestamp = nanos;

    for (std::size_t i = 0; i < sz; i++)
      bytes.push_back(data[i]);

    int read = 0;
    unsigned char* begin = bytes.data();
    unsigned char* end = bytes.data() + bytes.size();
    while ((read = parse(begin, end)) && read > 0)
    {
      begin += read;

      callback(std::move(msg));
      msg.clear();
    }

    // Remove the read bytes
    if (begin != bytes.data())
      bytes.erase(bytes.begin(), bytes.begin() + (begin - bytes.data()));
  }

  int parse(unsigned char* bytes, unsigned char* end)
  {
    int sz = end - bytes;
    if (sz == 0)
      return 0;

    msg.bytes.clear();

    if (((uint8_t)bytes[0] & 0xF) == 0xF)
    {
      // TODO special message
      return sz;
    }
    else if (((uint8_t)bytes[0] & 0xF8) == 0xF8)
    {
      // Clk messages
      msg.bytes.reserve(1);
      msg.bytes.push_back(*bytes++);
      runningStatusType_ = msg.bytes[0];

      return 1;
    }
    else
    {
      if (sz <= 1)
        return 0;

      // Normal message
      msg.bytes.reserve(3);

      // Setup first two bytes
      if (((uint8_t)bytes[0] & 0x80) == 0)
      {
        msg.bytes.push_back(runningStatusType_);
        msg.bytes.push_back(*bytes++);
      }
      else
      {
        if (sz < 2)
          return 0;

        msg.bytes.push_back(*bytes++);
        msg.bytes.push_back(*bytes++);
        runningStatusType_ = msg.bytes[0];
      }

      switch (message_type((uint8_t)runningStatusType_ & 0xF0))
      {
        case message_type::NOTE_OFF:
        case message_type::NOTE_ON:
        case message_type::POLY_PRESSURE:
        case message_type::CONTROL_CHANGE:
        case message_type::PITCH_BEND:
          if (sz < 3)
            return 0;

          msg.bytes.push_back(*bytes++);
          return 3;

        case message_type::PROGRAM_CHANGE:
        case message_type::AFTERTOUCH:
          return 2;

        default:
          // TODO
          return sz;
      }
    }
  }
};

}
