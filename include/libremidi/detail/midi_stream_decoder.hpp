#pragma once

#include <libremidi/cmidi2.hpp>
#include <libremidi/detail/conversion.hpp>
#include <libremidi/detail/midi_in.hpp>

#include <cmath>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>

NAMESPACE_LIBREMIDI
{
LIBREMIDI_STATIC int64_t system_ns() noexcept
{
  namespace clk = std::chrono;
  return clk::duration_cast<clk::nanoseconds>(clk::steady_clock::now().time_since_epoch()).count();
}

struct timestamp_backend_info
{
  // The API provides some kind of timestamping
  bool has_absolute_timestamps{};

  // The provided timestamping is equivalent or more precise than
  // e.g. clock_gettime(CLOCK_MONOTONIC)
  bool absolute_is_monotonic{};

  // The API can provide samples in a buffer cycle (only PipeWire and JACK so far)
  bool has_samples{};
};

template <typename Configuration>
struct input_state_machine_base
{
  const Configuration& configuration;

  explicit input_state_machine_base(const Configuration& conf)
      : configuration{conf}
  {
  }

  template <timestamp_backend_info info>
  int64_t timestamp(auto to_ns, int64_t samples)
  {
    switch (configuration.timestamps)
    {
      default:
      case timestamp_mode::NoTimestamp:
        return 0;

      case timestamp_mode::Relative: {
        int64_t time_ns;

        if constexpr (info.has_absolute_timestamps)
          time_ns = to_ns();
        else
          time_ns = system_ns();

        int64_t res;
        if (first_message)
        {
          first_message = false;
          res = 0;
        }
        else
        {
          res = time_ns - last_time_ns;
        }

        last_time_ns = time_ns;
        return res;
      }

      case timestamp_mode::Absolute:
        if constexpr (info.has_absolute_timestamps)
          return to_ns();
        else
          return system_ns();

      case timestamp_mode::SystemMonotonic:
        if constexpr (info.absolute_is_monotonic)
          return to_ns();
        else
          return system_ns();

      case timestamp_mode::AudioFrame:
        if constexpr (info.has_samples)
          return samples;
        else
          return 0;

      case timestamp_mode::Custom:
        return configuration.get_timestamp(to_ns());
    }
  }
  int64_t last_time_ns = 0;
  bool first_message = true;
};

namespace midi1
{
// Decodes MIDI 1.0 bytes into messages.
//
// Two entry points match the two kinds of input APIs:
//
// - on_bytes_multi() takes a MIDI 1.0 byte stream cut anywhere (rawio, Android, ALSA rawmidi,
//   CoreMIDI packets). A message cut by the end of a buffer is completed by the next one, and
//   running status applies: data bytes that follow a channel message without a status byte
//   reuse its status, until a system common or system-exclusive status cancels it.
//
// - on_bytes() takes what an event API delivers (JACK, PipeWire, ALSA sequencer, WinMM, Web
//   MIDI, network MIDI): each call holds whole messages that start with their status byte, or
//   a chunk of a system-exclusive message. Those APIs never use running status, so data bytes
//   that follow a complete message, or a call that starts with data bytes outside of a
//   system-exclusive message, are not messages and are dropped. They come from padding, or
//   from the continuation of a system-exclusive message that another source interrupted.
//
// Both paths deliver real-time bytes (F8 to FF) on their own wherever they appear, inside a
// system-exclusive message or between the bytes of any other message, which keeps being
// assembled. Any other status byte ends an unterminated system-exclusive message, which is
// dropped, and an F7 outside of one is ignored.
//
// A message carries the timestamp of the buffer that holds its last byte: a system-exclusive
// message spread over several buffers is stamped when its F7 arrives. Messages are delivered
// in the order they are completed, so their timestamps never go backwards.
//
// A system-exclusive message longer than input_configuration::sysex_max_size, or than what a
// message can hold with LIBREMIDI_SLIM_MESSAGE, is dropped and reported through on_warning.
struct input_state_machine : input_state_machine_base<input_configuration>
{
  using input_state_machine_base::input_state_machine_base;

  void reset()
  {
    message.bytes.clear();
    message.timestamp = {};
    m_state = main;
    m_sysex_dropped = false;
    m_partial = {};
  }

  void on_bytes_multi(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      decode<true>(this->configuration.on_message, bytes, timestamp, m_partial);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

  void on_bytes(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
    {
      partial_message partial{};
      decode<false>(this->configuration.on_message, bytes, timestamp, partial);
    }
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

private:
  struct partial_message
  {
    uint8_t bytes[3]{};
    uint8_t size{};
    uint8_t expected{};
    // Status of the last channel message, reused by running status
    uint8_t running{};
  };

  // Length of a message from its status byte, for any status but F0, F7 and real-time ones
  static constexpr uint8_t message_length(uint8_t status) noexcept
  {
    switch (status & 0xF0)
    {
      case 0xC0:
      case 0xD0:
        return 2;
      case 0xF0:
        switch (status)
        {
          case 0xF1:
          case 0xF3:
            return 2;
          case 0xF2:
            return 3;
          default:
            return 1;
        }
      default:
        return 3;
    }
  }

  static constexpr bool is_status(uint8_t byte) noexcept { return byte & 0x80; }

  // Running status is only meaningful for a stream: see the class comment.
  template <bool Stream>
  void decode(
      const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp,
      partial_message& partial)
  {
    const uint8_t* p = bytes.data();
    const uint8_t* const end = p + bytes.size();
    while (p != end)
    {
      if (m_state == in_sysex)
      {
        p = on_sysex(cb, p, end, timestamp);
        continue;
      }

      const uint8_t byte = *p++;
      if (byte >= 0xF8)
        on_realtime(cb, byte, timestamp);
      else if (is_status(byte))
        on_status(cb, byte, timestamp, partial);
      else
        on_data<Stream>(cb, byte, timestamp, partial);
    }
  }

  void on_status(
      const message_callback& cb, uint8_t status, int64_t timestamp, partial_message& partial)
  {
    // A status byte ends any message being assembled, complete or not.
    partial.size = 0;
    partial.running = status < 0xF0 ? status : 0;

    switch (status)
    {
      case 0xF0:
        start_sysex();
        return;
      case 0xF7:
        return;
      default:
        break;
    }

    const uint8_t length = message_length(status);
    if (length == 1)
    {
      deliver(cb, &status, 1, timestamp);
      return;
    }

    partial.bytes[0] = status;
    partial.size = 1;
    partial.expected = length;
  }

  template <bool Stream>
  void
  on_data(const message_callback& cb, uint8_t byte, int64_t timestamp, partial_message& partial)
  {
    if (partial.size == 0)
    {
      if constexpr (Stream)
      {
        if (partial.running == 0)
          return;
        partial.bytes[0] = partial.running;
        partial.size = 1;
        partial.expected = message_length(partial.running);
      }
      else
      {
        return;
      }
    }

    partial.bytes[partial.size++] = byte;
    if (partial.size == partial.expected)
    {
      partial.size = 0;
      deliver(cb, partial.bytes, partial.expected, timestamp);
    }
  }

  void deliver(const message_callback& cb, const uint8_t* bytes, uint8_t size, int64_t timestamp)
  {
    if (bytes[0] == 0xF1 && configuration.ignore_timing)
      return;

    libremidi::message msg;
    msg.bytes.assign(bytes, bytes + size);
    msg.timestamp = timestamp;
    cb(std::move(msg));
  }

  void on_realtime(const message_callback& cb, uint8_t status, int64_t timestamp)
  {
    if (status == 0xF8 && configuration.ignore_timing)
      return;
    if (status == 0xFE && configuration.ignore_sensing)
      return;

    libremidi::message msg;
    msg.bytes.assign(1, status);
    msg.timestamp = timestamp;
    cb(std::move(msg));
  }

  void start_sysex()
  {
    message.bytes.clear();
    m_sysex_dropped = false;
    m_state = in_sysex;
    append_sysex(nullptr, nullptr);
  }

  // Bytes a system-exclusive message may hold, F0 and F7 included
  std::size_t sysex_capacity() const noexcept
  {
    const std::size_t capacity = message.bytes.max_size();
    if (configuration.sysex_max_size == 0)
      return capacity;
    return std::min<std::size_t>(capacity, configuration.sysex_max_size);
  }

  // Appends F0 when the message is empty, then the data bytes, keeping room for F7.
  void append_sysex(const uint8_t* begin, const uint8_t* end)
  {
    if (configuration.ignore_sysex || m_sysex_dropped)
      return;

    const std::size_t head = message.bytes.empty() ? 1 : 0;
    const std::size_t count = static_cast<std::size_t>(end - begin);
    if (message.bytes.size() + head + count + 1 > sysex_capacity())
    {
      drop_sysex();
      return;
    }

    if (head)
      message.bytes.push_back(0xF0);
    message.bytes.insert(message.bytes.end(), begin, end);
  }

  void drop_sysex()
  {
    message.bytes.clear();
    m_sysex_dropped = true;

    const std::string_view text
        = "midi_in: an incoming system-exclusive message is longer than the maximum size and is "
          "dropped";
    if (configuration.on_warning)
      configuration.on_warning(text, source_location::current());
    else
      LIBREMIDI_LOG(text);
  }

  // Consumes bytes of the open system-exclusive message. Returns where decoding resumes:
  // after its F7, at the status byte that interrupts it, or at the end of the buffer.
  const uint8_t* on_sysex(
      const message_callback& cb, const uint8_t* p, const uint8_t* const end, int64_t timestamp)
  {
    while (p != end)
    {
      const uint8_t* const data_end
          = std::find_if(p, end, [](uint8_t byte) { return is_status(byte); });
      append_sysex(p, data_end);
      p = data_end;
      if (p == end)
        break;

      const uint8_t status = *p;
      if (status >= 0xF8)
      {
        ++p;
        on_realtime(cb, status, timestamp);
        // The callback may have fed the decoder and ended the message.
        if (m_state != in_sysex)
          break;
      }
      else if (status == 0xF7)
      {
        ++p;
        end_sysex(cb, timestamp);
        break;
      }
      else
      {
        // Unterminated: dropped, and the status is decoded on its own.
        message.bytes.clear();
        m_sysex_dropped = false;
        m_state = main;
        break;
      }
    }
    return p;
  }

  void end_sysex(const message_callback& cb, int64_t timestamp)
  {
    const bool keep = !configuration.ignore_sysex && !m_sysex_dropped;
    libremidi::message msg = std::move(message);
    message.bytes.clear();
    m_sysex_dropped = false;
    m_state = main;

    if (!keep)
      return;

    // append_sysex kept room for it
    msg.bytes.push_back(0xF7);
    msg.timestamp = timestamp;
    cb(std::move(msg));
  }

public:
  // Assembles the open system-exclusive message
  libremidi::message message;

private:
  partial_message m_partial;
  enum
  {
    main,
    in_sysex
  } m_state{main};
  bool m_sysex_dropped{};
};
}

namespace midi2
{
struct input_state_machine : input_state_machine_base<ump_input_configuration>
{
  using input_state_machine_base::input_state_machine_base;

  void reset() { m_carry_size = 0; }

  // Processes a stream of UMP words given as bytes in native byte order, which may be cut
  // anywhere: a word or a packet cut by the end of the buffer is completed by the next call.
  // on_raw_data receives the complete packets.
  void on_bytes_multi(std::span<const unsigned char> bytes, int64_t timestamp)
  {
    uint32_t words[64];
    auto* const storage = reinterpret_cast<unsigned char*>(words);
    while (!bytes.empty())
    {
      std::memcpy(storage, m_carry, m_carry_size);
      const std::size_t added = std::min(sizeof(words) - m_carry_size, bytes.size());
      std::memcpy(storage + m_carry_size, bytes.data(), added);
      bytes = bytes.subspan(added);

      const std::size_t n_bytes = m_carry_size + added;
      const std::size_t n_words = complete_packets(words, n_bytes / 4);
      m_carry_size = n_bytes - n_words * 4;
      std::memcpy(m_carry, storage + n_words * 4, m_carry_size);

      if (n_words > 0)
        on_bytes_multi(std::span<const uint32_t>{words, n_words}, timestamp);
    }
  }

  // Processes whole UMP packets, skipping NOOP padding. A packet cut short by the end of the
  // buffer is dropped.
  void on_bytes_multi(std::span<const uint32_t> words, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_packets<true>(this->configuration.on_message, words, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(words, timestamp);
  }

  // Processes whole UMP packets as an event API delivers them. A packet cut short by the end of
  // the buffer is dropped.
  void on_bytes(std::span<const uint32_t> words, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_packets<false>(this->configuration.on_message, words, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(words, timestamp);
  }

private:
  static std::size_t packet_words(uint32_t first_word) noexcept
  {
    return cmidi2_ump_get_num_bytes(first_word) / 4;
  }

  // Number of words, from the start, that form complete packets
  static std::size_t complete_packets(const uint32_t* words, std::size_t count) noexcept
  {
    std::size_t i = 0;
    while (i < count)
    {
      const std::size_t n = packet_words(words[i]);
      if (n > count - i)
        break;
      i += n;
    }
    return i;
  }

  template <bool SkipNoop>
  void on_packets(const ump_callback& cb, std::span<const uint32_t> words, int64_t timestamp)
  {
    while (!words.empty())
    {
      if constexpr (SkipNoop)
      {
        if (words[0] == 0)
        {
          words = words.subspan(1);
          continue;
        }
      }

      const std::size_t n = packet_words(words[0]);
      if (n > words.size())
        break;

      on_packet(cb, words.first(n), timestamp);
      words = words.subspan(n);
    }
  }

  // Processes one complete packet
  void on_packet(const ump_callback& cb, std::span<const uint32_t> packet, int64_t timestamp)
  {
    // Filter according to message type
    switch (cmidi2_ump_get_message_type(packet.data()))
    {
      case CMIDI2_MESSAGE_TYPE_UTILITY: {
        // All the utility messages are about timing
        if (this->configuration.ignore_timing)
          return;
        break;
      }

      case CMIDI2_MESSAGE_TYPE_SYSTEM: {
        auto status = cmidi2_ump_get_status_byte(packet.data());
        if (this->configuration.ignore_timing)
        {
          switch (status)
          {
            case CMIDI2_SYSTEM_STATUS_MIDI_TIME_CODE:
            case CMIDI2_SYSTEM_STATUS_SONG_POSITION:
            case CMIDI2_SYSTEM_STATUS_TIMING_CLOCK:
              return;
          }
        }

        if (this->configuration.ignore_sensing)
        {
          if (status == CMIDI2_SYSTEM_STATUS_ACTIVE_SENSING)
            return;
        }
        break;
      }

      case CMIDI2_MESSAGE_TYPE_SYSEX7:
      case CMIDI2_MESSAGE_TYPE_SYSEX8_MDS: {
        if (this->configuration.ignore_sysex)
          return;
        break;
      }

      case CMIDI2_MESSAGE_TYPE_MIDI_1_CHANNEL: {
        if (this->configuration.midi1_channel_events_to_midi2)
        {
          libremidi::ump msg;
          cmidi2_ump_upgrade_midi1_channel_voice_to_midi2(packet.data(), msg.data);
          msg.timestamp = timestamp;
          cb(std::move(msg));
          return;
        }
        break;
      }
    }

    libremidi::ump msg;
    std::copy_n(packet.begin(), std::min(packet.size(), std::size(msg.data)), msg.data);
    msg.timestamp = timestamp;
    cb(std::move(msg));
  }

  // Bytes of an incomplete packet, at most three words and three bytes
  unsigned char m_carry[16]{};
  std::size_t m_carry_size{};
};
}
}
