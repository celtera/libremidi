#pragma once

#include <libremidi/detail/midi_in.hpp>

#include <chrono>

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

namespace midi1
{
struct input_state_machine
{
  const input_configuration& configuration;
  explicit input_state_machine(const input_configuration& conf)
      : configuration{conf}
  {
  }

  bool has_finished_sysex(std::span<const uint8_t> bytes) const noexcept
  {
    return (((bytes.front() == 0xF0) || (state == in_sysex)) && (bytes.back() == 0xF7));
  }

  // Function to process a byte stream which may contain multiple successive
  // MIDI events (CoreMIDI, ALSA Sequencer can work like this)
  void on_bytes_multi(std::span<const uint8_t> bytes, int64_t timestamp)
  {
    int nBytes = bytes.size();
    int iByte = 0;

    const bool finished_sysex = has_finished_sysex(bytes);
    switch (state)
    {
      case in_sysex: {
        return on_continue_sysex(bytes, finished_sysex);
      }
      case main: {
        while (iByte < nBytes)
        {
          int size = 1;
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

  libremidi::message message;

  int64_t last_time_ns = 0;
  enum
  {
    main,
    in_sysex
  } state{main};

  bool first_message = true;
};
}
}
