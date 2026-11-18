#pragma once
#include <libremidi/cmidi2.hpp>
#include <libremidi/error.hpp>

#include <cstdint>
#include <span>

NAMESPACE_LIBREMIDI
{
enum class segmentation_error
{
  no_error,
  need_space,
  other
};

// Take the next contiguous run belonging to a port, without copying or splitting packets.
// A negative group accepts all packets. An incomplete final packet is discarded.
inline std::span<const uint32_t>
next_ump_group_run(std::span<const uint32_t>& words, int group)
{
  std::size_t accepted = 0;
  while (accepted < words.size())
  {
    const auto tail = words.subspan(accepted);
    const std::size_t n = cmidi2_ump_get_num_bytes(tail.front()) / sizeof(uint32_t);
    if (n > tail.size())
      break;
    if (group < 0 || cmidi2_ump_get_group(tail.data()) == group)
    {
      accepted += n;
      continue;
    }

    const auto run = words.first(accepted);
    words = tail.subspan(n);
    if (!run.empty())
      return run;
  }

  const auto run = words.first(accepted);
  words = {};
  return run;
}

/**
 * Utility function to segment an ump stream into individual messages.
 * Used to send a stream to APIs that work message-by-message.
 */
inline stdx::error
segment_ump_stream(const uint32_t* ump_stream, int64_t count, auto write_func, auto realloc_func)
{
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

    const auto ump_bytes = cmidi2_ump_get_num_bytes(ump_stream[0]);

    // A packet cut short by the end of the buffer is not sent.
    if (ump_bytes / 4 > count)
      break;

    // FIXME std::expected, propagate the error back to caller?
    switch (int err = static_cast<int>(write_func(ump_stream, ump_bytes)))
    {
      case 0:
        break;
      case -ENOMEM:
      case ENOMEM:
        // Try again if we didn't have enough space in the OS queue
        realloc_func();
        if (auto err = write_func(ump_stream, ump_bytes); err != std::errc{})
          return std::make_error_code(err);
        break;
      default:
        return from_errc(err);
    }

    const auto ump_uints = ump_bytes / 4;
    ump_stream += ump_uints;
    count -= ump_uints;
  }

  return stdx::error{};
}

}
