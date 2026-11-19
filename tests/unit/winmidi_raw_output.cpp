#include "../include_catch.hpp"

#include <libremidi/backends/winmidi/raw_output.hpp>

#include <algorithm>
#include <vector>

namespace
{
struct recording_endpoint
{
  struct call
  {
    uint64_t timestamp;
    uint32_t count;
    const uint32_t* words;
    bool operator==(const call&) const = default;
  };
  std::vector<call> calls;
  size_t fail_on_call{};

  int32_t SendMidiMessagesRaw(uint64_t timestamp, uint32_t count, const uint32_t* words)
  {
    calls.push_back({timestamp, count, words});
    return calls.size() == fail_on_call ? -1 : 0;
  }
};
}

TEST_CASE("Raw sends use maximal whole-packet prefixes", "[winmidi][raw_output]")
{
  // Widths: NOOP, 1, 2, 3, NOOP, 4, 1, NOOP. Zero payload words are not padding.
  const uint32_t words[]{0, 0x2590407f, 0x45904000, 0, 0xb0000000, 0, 0,
                         0, 0x50000000, 0, 0, 0, 0x10f80000, 0};
  recording_endpoint endpoint;
  const libremidi::winmidi::raw_output output{5, 42};
  REQUIRE(output.send(endpoint, words) == stdx::error{});
  const std::vector<recording_endpoint::call> expected{
      {42, 3, words + 1}, {42, 3, words + 4}, {42, 5, words + 8}};
  REQUIRE(endpoint.calls == expected);
}

TEST_CASE("Raw send errors never split a packet or retry a send", "[winmidi][raw_output]")
{
  recording_endpoint endpoint;
  const uint32_t words[]{0x2590407f, 0x50000000, 0, 0, 0, 0x10f80000};
  SECTION("A truncated tail is reported after the complete prefix")
  {
    const libremidi::winmidi::raw_output output{8, 0};
    REQUIRE(output.send(endpoint, {words, 4}) == stdx::error{std::errc::bad_message});
    REQUIRE(endpoint.calls == std::vector<recording_endpoint::call>{{0, 1, words}});
  }
  SECTION("A packet larger than the limit is never sent")
  {
    const libremidi::winmidi::raw_output output{3, 0};
    REQUIRE(output.send(endpoint, words) == stdx::error{std::errc::message_size});
    REQUIRE(endpoint.calls == std::vector<recording_endpoint::call>{{0, 1, words}});
  }
  SECTION("Zero capacity cannot send even a single word")
  {
    const libremidi::winmidi::raw_output output{0, 0};
    REQUIRE(output.send(endpoint, words) == stdx::error{std::errc::message_size});
    REQUIRE(endpoint.calls.empty());
  }
  SECTION("Empty or all-padding streams need no transmission")
  {
    const uint32_t padding[4]{};
    const libremidi::winmidi::raw_output output{4, 0};
    REQUIRE(output.send(endpoint, {}) == stdx::error{});
    REQUIRE(output.send(endpoint, padding) == stdx::error{});
    REQUIRE(endpoint.calls.empty());
  }
  SECTION("A failed second transmission prevents the third")
  {
    const libremidi::winmidi::raw_output output{4, 0};
    endpoint.fail_on_call = 2;
    REQUIRE(output.send(endpoint, words) == stdx::error{std::errc::io_error});
    const std::vector<recording_endpoint::call> expected{{0, 1, words}, {0, 4, words + 1}};
    REQUIRE(endpoint.calls == expected);
  }
}

TEST_CASE("64 KiB SysEx7 uses the negotiated capacity", "[winmidi][raw_output]")
{
  constexpr uint32_t data_bytes = 65536;
  constexpr uint32_t packets = (data_bytes + 5) / 6;
  std::vector<uint32_t> words(packets * 2);
  for (uint32_t i = 0; i < packets; ++i)
  {
    const bool last = i + 1 == packets;
    const uint32_t status = i == 0 ? 1 : last ? 3 : 2;
    const uint32_t bytes = last ? data_bytes - i * 6 : 6;
    words[2 * i] = 0x35000000 | (status << 20) | (bytes << 16);
  }

  recording_endpoint endpoint;
  const libremidi::winmidi::raw_output output{127, 0};
  REQUIRE(output.send(endpoint, words) == stdx::error{});
  REQUIRE(endpoint.calls.size() == 174);
  size_t offset = 0;
  for (const auto& call : endpoint.calls)
  {
    REQUIRE(call.words == words.data() + offset);
    REQUIRE(call.count == std::min<size_t>(126, words.size() - offset));
    offset += call.count;
  }
  REQUIRE(offset == words.size());
}
