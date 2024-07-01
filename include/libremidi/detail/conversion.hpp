#pragma once
// clang-format off
#include <libremidi/config.hpp>
// clang-format on

#include <libremidi/cmidi2.hpp>
#include <libremidi/error.hpp>
#include <libremidi/message.hpp>
#include <libremidi/ump.hpp>
namespace libremidi
{
struct midi1_to_midi2
{
  stdx::error
  convert(const unsigned char* message, std::size_t size, int64_t timestamp, auto on_ump)
  {
    uint32_t ump[65536 / 4];

    context.midi1 = const_cast<unsigned char*>(message);
    context.midi1_num_bytes = size;
    context.midi1_proceeded_bytes = 0;
    context.ump = ump;
    context.ump_num_bytes = sizeof(ump);
    context.ump_proceeded_bytes = 0;

    if (auto res = cmidi2_convert_midi1_to_ump(&context); res != CMIDI2_CONVERSION_RESULT_OK)
      return std::errc::invalid_argument;

    on_ump(context.ump, context.ump_proceeded_bytes / 4, timestamp);
    return stdx::error{};
  }

  cmidi2_midi_conversion_context context = [] {
    cmidi2_midi_conversion_context tmp;
    cmidi2_midi_conversion_context_initialize(&tmp);
    return tmp;
  }();
};

struct midi2_to_midi1
{
  stdx::error convert(const uint32_t* message, std::size_t size, int64_t timestamp, auto on_midi)
  {
    uint8_t midi[65536];
    const auto n
        = cmidi2_convert_single_ump_to_midi1(midi, sizeof(midi), const_cast<uint32_t*>(message));
    if (n > 0)
      return on_midi(midi, n, timestamp);
    else
      return std::errc::no_buffer_space;
  }

  cmidi2_midi_conversion_context context = [] {
    cmidi2_midi_conversion_context tmp;
    cmidi2_midi_conversion_context_initialize(&tmp);
    return tmp;
  }();
};

}
