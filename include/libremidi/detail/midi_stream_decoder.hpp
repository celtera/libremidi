#pragma once

#include <libremidi/cmidi2.hpp>
#include <libremidi/detail/midi_in.hpp>

#include <cmath>

#include <chrono>
#include <cstdint>
#include <span>

static inline void cmidi2_reverse(int64_t v, cmidi2_ump* output)
{
  union
  {
    uint32_t u[2];
    int64_t i;
  } e0, e1;
  e0.i = v;
  e1.u[0] = e0.u[1];
  e1.u[1] = e0.u[0];
  memcpy(output, &e1, 8);
}

static inline bool
cmidi2_ump_upgrade_midi1_channel_voice_to_midi2(const cmidi2_ump* input, cmidi2_ump* output)
{
  if (cmidi2_ump_get_message_type(input) != CMIDI2_MESSAGE_TYPE_MIDI_1_CHANNEL)
    return false;

  const auto group = cmidi2_ump_get_group(input);
  const auto channel = cmidi2_ump_get_channel(input);
  const auto b0 = cmidi2_ump_get_status_code(input);
  const auto b1 = cmidi2_ump_get_midi1_byte2(input);
  const auto b2 = cmidi2_ump_get_midi1_byte3(input);

  static constexpr auto u7_to_u16 = [](uint8_t in) -> uint16_t {
    static constexpr auto ratio = float(std::numeric_limits<uint16_t>::max()) / 127.f;
    return std::clamp(std::round(in * ratio), 0.f, float(std::numeric_limits<uint16_t>::max()));
  };
  static constexpr auto u7_to_u32 = [](uint8_t in) -> uint32_t {
    static constexpr auto ratio = double(std::numeric_limits<uint32_t>::max()) / 127.;
    return std::clamp(std::round(in * ratio), 0., double(std::numeric_limits<uint32_t>::max()));
  };
  static constexpr auto u16_to_u32 = [](uint16_t in) -> uint32_t {
    static constexpr auto ratio = double(std::numeric_limits<uint32_t>::max())
                                  / double(std::numeric_limits<uint16_t>::max());
    return std::clamp(std::round(in * ratio), 0., double(std::numeric_limits<uint32_t>::max()));
  };

  switch (b0)
  {
    // Not in midi 1:
    case CMIDI2_STATUS_PER_NOTE_RCC:
    case CMIDI2_STATUS_PER_NOTE_ACC:
    case CMIDI2_STATUS_RPN:
    case CMIDI2_STATUS_NRPN:
    case CMIDI2_STATUS_RELATIVE_RPN:
    case CMIDI2_STATUS_RELATIVE_NRPN:
    case CMIDI2_STATUS_PER_NOTE_PITCH_BEND:
    case CMIDI2_STATUS_PER_NOTE_MANAGEMENT:
      return false;

    case CMIDI2_STATUS_NOTE_OFF: {
      const int64_t msg = cmidi2_ump_midi2_note_off(group, channel, b1, 0, u7_to_u16(b2), 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_NOTE_ON: {
      const int64_t msg = cmidi2_ump_midi2_note_on(group, channel, b1, 0, u7_to_u16(b2), 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_PAF: {
      const int64_t msg = cmidi2_ump_midi2_paf(group, channel, b1, u7_to_u32(b2));
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_CC: {
      int64_t msg = cmidi2_ump_midi2_cc(group, channel, b1, u7_to_u32(b2));
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_PROGRAM: {
      const int64_t msg = cmidi2_ump_midi2_program(group, channel, 0, b1, 0, 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_CAF: {
      const int64_t msg = cmidi2_ump_midi2_caf(group, channel, u7_to_u32(b1));
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_PITCH_BEND: {
      const auto pb = b1 + b2 * 0x80;
      const int64_t msg = cmidi2_ump_midi2_pitch_bend_direct(group, channel, u16_to_u32(pb));
      cmidi2_reverse(msg, output);
      break;
    }
  }

  return true;
}

namespace libremidi
{
static inline int64_t system_ns() noexcept
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

template<typename Configuration>
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
struct input_state_machine : input_state_machine_base<input_configuration>
{
  using input_state_machine_base::input_state_machine_base;

  void reset()
  {
    message.bytes.clear();
    message.timestamp = {};
    m_state = main;
  }

  bool has_finished_sysex(std::span<const uint8_t> bytes) const noexcept
  {
    return (((bytes.front() == 0xF0) || (m_state == in_sysex)) && (bytes.back() == 0xF7));
  }

  // Function to process a byte stream which may contain multiple successive
  // MIDI events (CoreMIDI, ALSA Sequencer can work like this)
  void on_bytes_multi(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_multi_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

  // Function to process bytes corresponding to at most one midi event
  // e.g. a midi channel event or a single sysex
  void on_bytes(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

private:
  void on_bytes_multi_segmented(
      const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp)
  {
    int64_t n_bytes = bytes.size();
    int64_t i_byte = 0;

    const bool finished_sysex = has_finished_sysex(bytes);
    switch (m_state)
    {
      case in_sysex: {
        return on_continue_sysex(cb, bytes, finished_sysex);
      }
      case main: {
        while (i_byte < n_bytes)
        {
          int64_t size = 1;
          // We are expecting that the next byte in the packet is a status
          // byte.
          const auto status = bytes[i_byte];
          if (!(status & 0x80))
            break;

          // Determine the number of bytes in the MIDI message.
          if (status < 0xC0)
            size = 3;
          else if (status < 0xE0)
            size = 2;
          else if (status < 0xF0)
            size = 3;
          else if (status == 0xF0)
          {
            if (configuration.ignore_sysex)
            {
              size = 0;
              i_byte = n_bytes;
            }
            else
            {
              size = n_bytes - i_byte;
            }

            if (bytes[n_bytes - 1] != 0xF7)
            {
              // We know per CoreMIDI API there can't be anything else in this packet
              m_state = in_sysex;
              message.assign(bytes.begin(), bytes.begin() + size);
              message.timestamp = timestamp;
              return;
            }
          }
          else if (status == 0xF1)
          {
            // A MIDI time code message
            if (configuration.ignore_timing)
            {
              size = 0;
              i_byte += 2;
            }
            else
            {
              size = 2;
            }
          }
          else if (status == 0xF2)
            size = 3;
          else if (status == 0xF3)
            size = 2;
          else if (status == 0xF8)
          {
            // A MIDI timing tick message
            if (configuration.ignore_timing)
            {
              size = 0;
              i_byte += 1;
            }
            else
            {
              size = 1;
            }
          }
          else if (status == 0xFE)
          {
            // A MIDI active sensing message
            if (configuration.ignore_sensing)
            {
              size = 0;
              i_byte += 1;
            }
            else
            {
              size = 1;
            }
          }
          else
          {
            // Remaining real-time messages
            size = 1;
          }

          // Now process the actual bytes of the message
          if (size > 0)
          {
            auto begin = bytes.begin() + i_byte;
            message.assign(begin, begin + size);
            message.timestamp = timestamp;

            cb(std::move(message));
            message.clear();

            i_byte += size;
          }
        }
      }
    }
  }

  void on_continue_sysex(
      const message_callback& cb, std::span<const uint8_t> bytes, bool finished_sysex)
  {
    if (finished_sysex)
      m_state = main;

    if (configuration.ignore_sysex)
    {
      return;
    }
    else
    {
      message.insert(message.end(), bytes.begin(), bytes.end());
      if (finished_sysex)
      {
        cb(std::move(message));
        message.clear();
      }
    }
    return;
  }

  void on_main(
      const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp,
      bool finished_sysex)
  {
    switch (bytes[0])
    {
      // SYSEX start
      case 0xF0: {
        if (!finished_sysex)
          m_state = in_sysex;

        if (!this->configuration.ignore_sysex)
        {
          message.assign(bytes.begin(), bytes.end());
          message.timestamp = timestamp;
          if (finished_sysex)
          {
            cb(std::move(message));
            message.clear();
          }
        }

        return;
      }

      case 0xF1:
      case 0xF8:
        if (this->configuration.ignore_timing)
          return;
        break;

      case 0xFE:
        if (this->configuration.ignore_sensing)
          return;
        break;

      default:
        break;
    }

    message.assign(bytes.begin(), bytes.end());
    message.timestamp = timestamp;

    cb(std::move(message));
    message.clear();
  }

  void
  on_bytes_segmented(const message_callback& cb, std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (bytes.empty())
      return;

    const bool finished_sysex = has_finished_sysex(bytes);
    switch (m_state)
    {
      case in_sysex:
        return on_continue_sysex(cb, bytes, finished_sysex);

      case main:
        return on_main(cb, bytes, timestamp, finished_sysex);
    }
  }

public:
  libremidi::message message;

private:
  enum
  {
    main,
    in_sysex
  } m_state{main};
};
}

namespace midi2
{
struct input_state_machine : input_state_machine_base<ump_input_configuration>
{
  using input_state_machine_base::input_state_machine_base;

public:
  void on_bytes_multi(std::span<const unsigned char> bytes, int64_t timestamp)
  {
    auto ptr = reinterpret_cast<const uint32_t*>(bytes.data());
    auto sz = bytes.size() / 4;
    return on_bytes_multi({ptr, sz}, timestamp);
  }

  void on_bytes_multi(std::span<const uint32_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_multi_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

  void on_bytes(std::span<const uint32_t> bytes, int64_t timestamp)
  {
    if (this->configuration.on_message)
      on_bytes_segmented(this->configuration.on_message, bytes, timestamp);
    if (this->configuration.on_raw_data)
      this->configuration.on_raw_data(bytes, timestamp);
  }

private:
  // Function to process a byte stream which may contain multiple successive
  // MIDI events (CoreMIDI, ALSA Sequencer can work like this)
  void on_bytes_multi_segmented(
      const ump_callback& cb, std::span<const uint32_t> bytes, int64_t timestamp)
  {
    auto count = bytes.size();
    auto ump_stream = bytes.data();
    while (count > 0)
    {
      // Handle NOOP (or padding)
      while (count > 0 && ump_stream[0] == 0)
      {
        count--;
        ump_stream++;
      }

      if (count == 0)
        break;

      const auto ump_uints = cmidi2_ump_get_num_bytes(ump_stream[0]) / 4;
      on_bytes_segmented(cb, {ump_stream, ump_stream + ump_uints}, timestamp);

      ump_stream += ump_uints;
      count -= ump_uints;
    }
  }

  // Function to process bytes corresponding to at most one midi event
  void
  on_bytes_segmented(const ump_callback& cb, std::span<const uint32_t> bytes, int64_t timestamp)
  {
    // Filter according to message type
    switch(cmidi2_ump_get_message_type(bytes.data()))
    {
      case CMIDI2_MESSAGE_TYPE_UTILITY:
      {
        // All the utility messages are about timing
        if (this->configuration.ignore_timing)
          return;
        break;
      }

      case CMIDI2_MESSAGE_TYPE_SYSTEM:
      {
        if (this->configuration.ignore_timing)
        {
          auto status = cmidi2_ump_get_system_message_byte2(bytes.data());
          switch(status)
          {
            case CMIDI2_SYSTEM_STATUS_MIDI_TIME_CODE:
            case CMIDI2_SYSTEM_STATUS_SONG_POSITION:
            case CMIDI2_SYSTEM_STATUS_TIMING_CLOCK:
              return;
          }
        }

       if (this->configuration.ignore_sensing)
       {
         auto status = cmidi2_ump_get_system_message_byte2(bytes.data());
         if(status == CMIDI2_SYSTEM_STATUS_ACTIVE_SENSING)
           return;
       }
       break;
      }

      case CMIDI2_MESSAGE_TYPE_SYSEX7:
      case CMIDI2_MESSAGE_TYPE_SYSEX8_MDS:
      {
        if (this->configuration.ignore_sysex)
          return;
        break;
      }

      case CMIDI2_MESSAGE_TYPE_MIDI_1_CHANNEL: {
        if (this->configuration.midi1_channel_events_to_midi2)
        {
          libremidi::ump msg;
          cmidi2_ump_upgrade_midi1_channel_voice_to_midi2(bytes.data(), msg.data);
          msg.timestamp = timestamp;
          cb(std::move(msg));
          return;
        }
        break;
      }
    }

    libremidi::ump msg;
    std::copy(bytes.begin(), bytes.end(), msg.data);
    msg.timestamp = timestamp;
    cb(std::move(msg));
  }
};
}
}
