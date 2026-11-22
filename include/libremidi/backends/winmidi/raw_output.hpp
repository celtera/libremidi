#pragma once
#include <libremidi/cmidi2.hpp>
#include <libremidi/error.hpp>

#include <span>

NAMESPACE_LIBREMIDI::winmidi
{
// Both values are obtained at open, never on the send path. The endpoint is borrowed.
struct raw_output
{
  uint32_t max_words{};
  uint64_t send_immediately{};

  stdx::error send(auto& endpoint, std::span<const uint32_t> words) const
  {
    while (!words.empty())
    {
      // Like segment_ump_stream, omit NOOP/padding only at a packet boundary.
      if (words.front() == 0)
      {
        words = words.subspan(1);
        continue;
      }

      uint32_t count = 0;
      while (count < words.size() && words[count] != 0)
      {
        const uint32_t n = cmidi2_ump_get_num_bytes(words[count]) / sizeof(uint32_t);
        if (n > words.size() - count)
        {
          if (count == 0)
            return std::errc::bad_message;
          break;
        }
        if (n > max_words - count)
        {
          if (count == 0)
            return std::errc::message_size;
          break;
        }
        count += n;
      }

      // We have already validated the length of each UMP while collecting this prefix.
      if (endpoint.SendMidiMessagesRaw(send_immediately, count, words.data()) < 0)
        return std::errc::io_error;
      words = words.subspan(count);
    }
    return {};
  }
};
}
