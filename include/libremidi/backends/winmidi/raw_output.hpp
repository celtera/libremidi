#pragma once
#include <libremidi/cmidi2.hpp>
#include <libremidi/error.hpp>

#include <algorithm>
#include <span>

NAMESPACE_LIBREMIDI::winmidi
{
// A connection accepts MAXIMUM_LOOPED_UMP_DATASIZE (2736) bytes per send, but
// the service then translates the buffer for each kind of client and checks the
// result again: 2048 bytes (MAXIMUM_LOOPED_BYTESTREAM_DATASIZE) for a MIDI 1
// byte stream client, 2736 bytes of UMP for a MIDI 1 protocol endpoint.
// 684 words of SysEx7 or of MIDI 1 channel messages already give 2052 bytes,
// and a MIDI 2 registered controller becomes up to four control changes:
// 6 bytes, or 2 words, for each of its words.
// The service bounds what it coalesces itself to a quarter of the UMP size,
// 171 words: at most 1026 bytes or 342 words once translated.
inline constexpr uint32_t max_batch_words = 2736 / 4 / sizeof(uint32_t);

constexpr uint32_t batch_words(uint32_t connection_max_words) noexcept
{
  return std::min(connection_max_words, max_batch_words);
}

// Both values are obtained at open, never on the send path. The endpoint is borrowed.
// max_words is the result of batch_words.
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
