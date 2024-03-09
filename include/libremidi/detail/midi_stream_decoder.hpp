#pragma once

#include <libremidi/detail/midi_in.hpp>

#include <libremidi/cmidi2.hpp>
#include <chrono>
#include <cstdint>
#include <cinttypes>
#include <span>

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
  bool has_finished_sysex(std::span<const uint8_t> bytes) const noexcept
  {
    return (((bytes.front() == 0xF0) || (state == in_sysex)) && (bytes.back() == 0xF7));
  }

  // Function to process a byte stream which may contain multiple successive
  // MIDI events (CoreMIDI, ALSA Sequencer can work like this)
  void on_bytes_multi(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    int64_t nBytes = bytes.size();
    int64_t iByte = 0;

    const bool finished_sysex = has_finished_sysex(bytes);
    switch (state)
    {
      case in_sysex: {
        return on_continue_sysex(bytes, finished_sysex);
      }
      case main: {
        while (iByte < nBytes)
        {
          int64_t size = 1;
          // We are expecting that the next byte in the packet is a status
          // byte.
          const auto status = bytes[iByte];
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
              iByte = nBytes;
            }
            else
            {
              size = nBytes - iByte;
            }

            if (bytes[nBytes - 1] != 0xF7)
            {
              // We know per CoreMIDI API there can't be anything else in this packet
              state = in_sysex;
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
              iByte += 2;
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
              iByte += 1;
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
              iByte += 1;
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
            auto begin = bytes.begin() + iByte;
            message.assign(begin, begin + size);
            message.timestamp = timestamp;

            this->configuration.on_message(std::move(message));
            message.clear();

            iByte += size;
          }
        }
      }
    }
  }

  void on_continue_sysex(std::span<const uint8_t> bytes, bool finished_sysex)
  {
    if (finished_sysex)
      state = main;

    if (configuration.ignore_sysex)
    {
      return;
    }
    else
    {
      message.insert(message.end(), bytes.begin(), bytes.end());
      if (finished_sysex)
      {
        this->configuration.on_message(std::move(message));
        message.clear();
      }
    }
    return;
  }

  void on_main(std::span<const uint8_t> bytes, int64_t timestamp, bool finished_sysex)
  {
    switch (bytes[0])
    {
      // SYSEX start
      case 0xF0: {
        if (!finished_sysex)
          state = in_sysex;

        if (!this->configuration.ignore_sysex)
        {
          message.assign(bytes.begin(), bytes.end());
          message.timestamp = timestamp;
          if (finished_sysex)
          {
            this->configuration.on_message(std::move(message));
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

    this->configuration.on_message(std::move(message));
    message.clear();
  }

  // Function to process bytes corresponding to at most one midi event
  // e.g. a midi channel event or a single sysex
  void on_bytes(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    if (bytes.empty())
      return;

    const bool finished_sysex = has_finished_sysex(bytes);
    switch (state)
    {
      case in_sysex:
        return on_continue_sysex(bytes, finished_sysex);

      case main:
        return on_main(bytes, timestamp, finished_sysex);
    }
  }

  libremidi::message message;

  enum
  {
    main,
    in_sysex
  } state{main};

};
}

namespace midi2
{
struct input_state_machine : input_state_machine_base<ump_input_configuration>
{
  using input_state_machine_base::input_state_machine_base;

  // Function to process a byte stream which may contain multiple successive
  // MIDI events (CoreMIDI, ALSA Sequencer can work like this)

  void on_bytes_multi(std::span<const unsigned char> bytes, int64_t timestamp)
  {
    auto ptr = reinterpret_cast<const uint32_t*>(bytes.data());
    auto sz = bytes.size() / 4;
    return on_bytes_multi({ptr, sz}, timestamp);
  }

  void on_bytes_multi(std::span<const uint32_t> bytes, int64_t timestamp)
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
      on_bytes({ump_stream, ump_stream + ump_uints}, timestamp);

      ump_stream += ump_uints;
      count -= ump_uints;
    }
  }

  // Function to process bytes corresponding to at most one midi event
  void on_bytes(std::span<const uint32_t> bytes, int64_t timestamp)
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
    }

    libremidi::ump msg;
    std::copy(bytes.begin(), bytes.end(), msg.data);
    msg.timestamp = timestamp;
    configuration.on_message(std::move(msg));
  }
};
}
}
