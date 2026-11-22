#include "../include_catch.hpp"

#include <libremidi/detail/ump_stream.hpp>

#include <utility>
#include <vector>

namespace
{
// What the backends that send packet by packet do with a stream.
struct recording_sink
{
  std::vector<std::pair<const uint32_t*, int>> packets;
  std::size_t fail_on_packet{};

  stdx::error send(const uint32_t* words, std::size_t count)
  {
    return libremidi::segment_ump_stream(words, count, [&](const uint32_t* ump, int bytes) -> stdx::error {
      packets.emplace_back(ump, bytes);
      if (packets.size() == fail_on_packet)
        return std::errc::io_error;
      return {};
    });
  }
};
using packets = std::vector<std::pair<const uint32_t*, int>>;
}

TEST_CASE("A stream is cut at the length of each message type", "[ump_stream]")
{
  // One packet of each message type, 0x1 to 0xF.
  const uint32_t words[]{
      0x10f80000, 0x2090407f, 0x30000000, 0, 0x40904000, 0, 0x50000000, 0, 0, 0,
      0x60000000, 0x70000000, 0x80000000, 0, 0x90000000, 0, 0xa0000000, 0,
      0xb0000000, 0, 0, 0xc0000000, 0, 0, 0xd0000000, 0, 0, 0,
      0xe0000000, 0, 0, 0, 0xf0000000, 0, 0, 0};
  recording_sink sink;
  REQUIRE(sink.send(words, std::size(words)) == stdx::error{});
  const packets expected{
      {words, 4},       {words + 1, 4},   {words + 2, 8},   {words + 4, 8},   {words + 6, 16},
      {words + 10, 4},  {words + 11, 4},  {words + 12, 8},  {words + 14, 8},  {words + 16, 8},
      {words + 18, 12}, {words + 21, 12}, {words + 24, 16}, {words + 28, 16}, {words + 32, 16}};
  REQUIRE(sink.packets == expected);
}

TEST_CASE("Padding is skipped between packets, never inside them", "[ump_stream]")
{
  const uint32_t words[]{0, 0, 0x2090407f, 0, 0x40904000, 0, 0, 0x50000000, 0, 0, 0, 0};
  recording_sink sink;
  REQUIRE(sink.send(words, std::size(words)) == stdx::error{});
  REQUIRE(sink.packets == packets{{words + 2, 4}, {words + 4, 8}, {words + 7, 16}});
}

TEST_CASE("Empty and all-padding streams send nothing", "[ump_stream]")
{
  const uint32_t padding[3]{};
  recording_sink sink;
  REQUIRE(sink.send(padding, 0) == stdx::error{});
  REQUIRE(sink.send(padding, std::size(padding)) == stdx::error{});
  REQUIRE(sink.packets.empty());
}

TEST_CASE("A truncated tail is reported after the complete prefix", "[ump_stream]")
{
  const uint32_t words[]{0x2090407f, 0x40904000, 0, 0x50000000, 0, 0};
  recording_sink sink;
  REQUIRE(sink.send(words, std::size(words)) == stdx::error{std::errc::bad_message});
  REQUIRE(sink.packets == packets{{words, 4}, {words + 1, 8}});

  SECTION("Also when nothing precedes it")
  {
    recording_sink alone;
    REQUIRE(alone.send(words + 1, 1) == stdx::error{std::errc::bad_message});
    REQUIRE(alone.packets.empty());
  }
}

TEST_CASE("The first failed write stops the stream and is returned", "[ump_stream]")
{
  const uint32_t words[]{0x2090407f, 0x40904000, 0, 0x2080407f};
  recording_sink sink;
  sink.fail_on_packet = 2;
  REQUIRE(sink.send(words, std::size(words)) == stdx::error{std::errc::io_error});
  REQUIRE(sink.packets == packets{{words, 4}, {words + 1, 8}});
}
