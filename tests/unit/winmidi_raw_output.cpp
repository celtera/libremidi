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

// The bound holds for the service's two translated buffers: 6 bytes of MIDI 1
// byte stream, or 2 words of MIDI 1 protocol UMP, for each word sent.
static_assert(libremidi::winmidi::max_batch_words == 171);
static_assert(libremidi::winmidi::max_batch_words * 6 <= 2048);
static_assert(libremidi::winmidi::max_batch_words * 2 * sizeof(uint32_t) <= 2736);
static_assert(libremidi::winmidi::batch_words(684) == 171);
static_assert(libremidi::winmidi::batch_words(171) == 171);
static_assert(libremidi::winmidi::batch_words(172) == 171);
static_assert(libremidi::winmidi::batch_words(170) == 170);
static_assert(libremidi::winmidi::batch_words(0) == 0);

TEST_CASE("Batches stop at the service's translation bound", "[winmidi][raw_output]")
{
  using libremidi::winmidi::batch_words;
  using libremidi::winmidi::max_batch_words;
  using call = recording_endpoint::call;
  constexpr uint32_t note = 0x2090407f, sysex7[2]{0x30260102, 0x03040506};
  recording_endpoint endpoint;
  // What the SDK reports for a connection.
  const libremidi::winmidi::raw_output output{batch_words(684), 7};

  SECTION("One word less than the bound, the bound, one word more")
  {
    std::vector<uint32_t> words(max_batch_words + 1, note);
    const auto* w = words.data();

    REQUIRE(output.send(endpoint, {w, max_batch_words - 1}) == stdx::error{});
    REQUIRE(endpoint.calls == std::vector<call>{{7, max_batch_words - 1, w}});

    endpoint.calls.clear();
    REQUIRE(output.send(endpoint, {w, max_batch_words}) == stdx::error{});
    REQUIRE(endpoint.calls == std::vector<call>{{7, max_batch_words, w}});

    endpoint.calls.clear();
    REQUIRE(output.send(endpoint, words) == stdx::error{});
    REQUIRE(endpoint.calls == std::vector<call>{{7, max_batch_words, w}, {7, 1, w + max_batch_words}});
  }
  SECTION("A packet that would straddle the bound starts the next batch")
  {
    std::vector<uint32_t> words(max_batch_words - 1, note);
    words.insert(words.end(), std::begin(sysex7), std::end(sysex7));
    words.push_back(note);
    const auto* w = words.data();
    REQUIRE(output.send(endpoint, words) == stdx::error{});
    REQUIRE(endpoint.calls == std::vector<call>{{7, max_batch_words - 1, w}, {7, 3, w + max_batch_words - 1}});
  }
  SECTION("A packet that ends on the bound stays in the batch")
  {
    std::vector<uint32_t> words(max_batch_words - 2, note);
    words.insert(words.end(), std::begin(sysex7), std::end(sysex7));
    words.push_back(note);
    const auto* w = words.data();
    REQUIRE(output.send(endpoint, words) == stdx::error{});
    REQUIRE(endpoint.calls == std::vector<call>{{7, max_batch_words, w}, {7, 1, w + max_batch_words}});
  }
  SECTION("A lower connection maximum wins")
  {
    const libremidi::winmidi::raw_output small{batch_words(5), 7};
    const std::vector<uint32_t> words{note, sysex7[0], sysex7[1], sysex7[0], sysex7[1], note, note};
    const auto* w = words.data();
    REQUIRE(small.send(endpoint, words) == stdx::error{});
    REQUIRE(endpoint.calls == std::vector<call>{{7, 5, w}, {7, 2, w + 5}});
  }
  SECTION("A full-size SysEx7 batch fits a MIDI 1 byte stream")
  {
    // 1 MiB of SysEx7 continue packets, 6 data bytes each.
    std::vector<uint32_t> words;
    for (int i = 0; i < 174763; ++i)
      words.insert(words.end(), {0x30260102u, 0x03040506u});
    REQUIRE(output.send(endpoint, words) == stdx::error{});
    for (const auto& c : endpoint.calls)
      REQUIRE(c.count / 2 * 6 <= 2048);
    REQUIRE(endpoint.calls.front().count == 170);
  }
}
