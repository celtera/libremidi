// Tests of the MIDI 1 decoder (libremidi::midi1::input_state_machine) against the MIDI 1.0
// specification: SysEx framing, real-time bytes inside other messages, running status,
// messages cut across buffers, filtering, state handling, random streams and fuzzing.
// on_bytes_multi() decodes a byte stream; on_bytes() decodes what an event API delivers,
// whole messages or SysEx chunks, without running status (see midi_stream_decoder.hpp).
// The throughput case is tagged [bench] and is not part of the default run.
#include "../include_catch.hpp"

#include <libremidi/detail/midi_stream_decoder.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using bytes_t = std::vector<uint8_t>;

struct out_msg
{
  bytes_t b;
  int64_t ts{};
  bool operator==(const out_msg&) const = default;
};

std::string hex(const bytes_t& b)
{
  std::string s;
  char buf[4];
  for (auto x : b)
  {
    std::snprintf(buf, sizeof(buf), "%02X ", x);
    s += buf;
  }
  if (s.size() > 120)
    s = s.substr(0, 120) + "...(" + std::to_string(b.size()) + " bytes)";
  return s;
}

std::string dump(const std::vector<out_msg>& v)
{
  std::ostringstream o;
  o << v.size() << " msgs\n";
  for (auto& m : v)
    o << "  [ts " << m.ts << "] " << hex(m.b) << "\n";
  return o.str();
}

struct flags
{
  bool ignore_sysex = false;
  bool ignore_timing = false;
  bool ignore_sensing = false;
};

struct harness
{
  libremidi::input_configuration conf;
  std::vector<out_msg> out;
  std::optional<libremidi::midi1::input_state_machine> sm;

  explicit harness(flags f = {})
  {
    conf.on_message = [this](libremidi::message&& m) {
      out.push_back({bytes_t(m.bytes.begin(), m.bytes.end()), m.timestamp});
    };
    conf.timestamps = libremidi::timestamp_mode::NoTimestamp;
    conf.ignore_sysex = f.ignore_sysex;
    conf.ignore_timing = f.ignore_timing;
    conf.ignore_sensing = f.ignore_sensing;
    sm.emplace(conf);
  }
  harness(const harness&) = delete;

  // Copies into an exactly-sized heap buffer so ASan sees any over-read.
  void feed(bool multi, const bytes_t& b, int64_t ts = 0)
  {
    auto* heap = new uint8_t[b.size() ? b.size() : 1];
    std::copy(b.begin(), b.end(), heap);
    std::span<const uint8_t> s{heap, b.size()};
    if (multi)
      sm->on_bytes_multi(s, ts);
    else
      sm->on_bytes(s, ts);
    delete[] heap;
  }
};

#define PATHS()                             \
  const bool multi = GENERATE(false, true); \
  CAPTURE(multi)

#define CHECK_OUT(h, ...)                                              \
  do                                                                   \
  {                                                                    \
    const std::vector<out_msg> expected_ = __VA_ARGS__;                \
    INFO("got: " << dump((h).out) << "expected: " << dump(expected_)); \
    CHECK((h).out == expected_);                                       \
  } while (0)
}

// ---------------------------------------------------------------------------
// SysEx framing
// ---------------------------------------------------------------------------

TEST_CASE("R01 complete sysex in one buffer", "[decoder][sysex]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x7D, 0x01, 0xF7}, 3);
  CHECK_OUT(h, {{{0xF0, 0x7D, 0x01, 0xF7}, 3}});
}

TEST_CASE("R02 empty sysex F0 F7", "[decoder][sysex]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0xF7}, 1);
  h.feed(multi, {0x90, 0x40, 0x7F}, 2);
  CHECK_OUT(h, {{{0xF0, 0xF7}, 1}, {{0x90, 0x40, 0x7F}, 2}});
}

TEST_CASE(
    "R03 sysex split in two at every position, timestamp of the buffer holding F7",
    "[decoder][sysex][split]")
{
  PATHS();
  const bytes_t sx{0xF0, 0x7D, 0x01, 0x02, 0x03, 0xF7};
  for (std::size_t cut = 1; cut < sx.size(); cut++)
  {
    CAPTURE(cut);
    harness h;
    h.feed(multi, bytes_t(sx.begin(), sx.begin() + cut), 10);
    h.feed(multi, bytes_t(sx.begin() + cut, sx.end()), 20);
    h.feed(multi, {0x90, 0x40, 0x7F}, 30);
    CHECK_OUT(h, {{sx, 20}, {{0x90, 0x40, 0x7F}, 30}});
  }
}

TEST_CASE("R04 sysex fed one byte per buffer", "[decoder][sysex][split]")
{
  PATHS();
  const bytes_t sx{0xF0, 0x7D, 0x01, 0x02, 0x03, 0xF7};
  harness h;
  int64_t ts = 100;
  for (auto b : sx)
    h.feed(multi, {b}, ts++);
  CHECK_OUT(h, {{sx, 105}});
}

TEST_CASE("R05 F0 F0 restarts the sysex", "[decoder][sysex]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01, 0xF0, 0x02, 0xF7}, 1);
  CHECK_OUT(h, {{{0xF0, 0x02, 0xF7}, 1}});
}

TEST_CASE("R06 two sysex back to back in one buffer", "[decoder][sysex]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01, 0xF7, 0xF0, 0x02, 0xF7}, 1);
  CHECK_OUT(h, {{{0xF0, 0x01, 0xF7}, 1}, {{0xF0, 0x02, 0xF7}, 1}});
}

TEST_CASE(
    "R07 sysex ends in one buffer, next starts in the same buffer", "[decoder][sysex][split]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01}, 1);
  h.feed(multi, {0x02, 0xF7, 0xF0, 0x03}, 2);
  h.feed(multi, {0x04, 0xF7}, 3);
  CHECK_OUT(h, {{{0xF0, 0x01, 0x02, 0xF7}, 2}, {{0xF0, 0x03, 0x04, 0xF7}, 3}});
}

// ---------------------------------------------------------------------------
// Real-time inside SysEx
// ---------------------------------------------------------------------------

TEST_CASE(
    "R10 every real-time status at every position inside a sysex", "[decoder][sysex][realtime]")
{
  PATHS();
  const bytes_t sx{0xF0, 0x7D, 0x01, 0x02, 0xF7};
  for (uint8_t rt : {0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF})
    for (std::size_t pos = 1; pos < sx.size(); pos++)
    {
      CAPTURE((int)rt, pos);
      bytes_t in = sx;
      in.insert(in.begin() + pos, rt);
      harness h;
      h.feed(multi, in, 7);
      CHECK_OUT(h, {{{rt}, 7}, {sx, 7}});
    }
}

TEST_CASE(
    "R11 real-time in its own buffer between sysex chunks (ALSA seq / WinMM MIM_DATA)",
    "[decoder][sysex][realtime]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01}, 1);
  h.feed(multi, {0xF8}, 2);
  h.feed(multi, {0x02, 0xF7}, 3);
  CHECK_OUT(h, {{{0xF8}, 2}, {{0xF0, 0x01, 0x02, 0xF7}, 3}});
}

TEST_CASE(
    "R12 ignored real-time inside sysex is dropped and not appended",
    "[decoder][sysex][realtime][ignore]")
{
  PATHS();
  SECTION("ignore_timing")
  {
    harness h{{.ignore_timing = true}};
    h.feed(multi, {0xF0, 0x01, 0xF8, 0x02, 0xF7}, 1);
    CHECK_OUT(h, {{{0xF0, 0x01, 0x02, 0xF7}, 1}});
  }
  SECTION("ignore_sensing")
  {
    harness h{{.ignore_sensing = true}};
    h.feed(multi, {0xF0, 0x01, 0xFE, 0x02, 0xF7}, 1);
    CHECK_OUT(h, {{{0xF0, 0x01, 0x02, 0xF7}, 1}});
  }
  SECTION("ignore_sensing keeps clock")
  {
    harness h{{.ignore_sensing = true}};
    h.feed(multi, {0xF0, 0x01, 0xF8, 0xFE, 0x02, 0xF7}, 1);
    CHECK_OUT(h, {{{0xF8}, 1}, {{0xF0, 0x01, 0x02, 0xF7}, 1}});
  }
}

TEST_CASE(
    "R13 ignore_sysex still delivers real-time inside it and what follows",
    "[decoder][sysex][ignore]")
{
  PATHS();
  harness h{{.ignore_sysex = true}};
  h.feed(multi, {0xF0, 0x01, 0xF8, 0x02}, 1);
  h.feed(multi, {0x03, 0xFA, 0xF7}, 2);
  h.feed(multi, {0x90, 0x40, 0x7F}, 3);
  CHECK_OUT(h, {{{0xF8}, 1}, {{0xFA}, 2}, {{0x90, 0x40, 0x7F}, 3}});
}

TEST_CASE(
    "R14 ignore_sysex with a sysex followed by a note in the same buffer",
    "[decoder][sysex][ignore]")
{
  harness h{{.ignore_sysex = true}};
  h.feed(true, {0xF0, 0x01, 0x02, 0xF7, 0x90, 0x40, 0x7F}, 1);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}});
}

// ---------------------------------------------------------------------------
// Other status bytes end a SysEx
// ---------------------------------------------------------------------------

TEST_CASE("R20 a channel status aborts the sysex and is decoded", "[decoder][sysex][abort]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01, 0x02}, 1);
  h.feed(multi, {0x90, 0x40, 0x7F}, 2);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 2}});
}

TEST_CASE("R21 each system common status aborts the sysex (multi)", "[decoder][sysex][abort]")
{
  struct c
  {
    bytes_t msg;
  };
  for (auto m :
       {bytes_t{0xF1, 0x23}, bytes_t{0xF2, 0x01, 0x02}, bytes_t{0xF3, 0x05}, bytes_t{0xF4},
        bytes_t{0xF5}, bytes_t{0xF6}})
  {
    CAPTURE(hex(m));
    harness h;
    bytes_t in{0xF0, 0x01, 0x02};
    in.insert(in.end(), m.begin(), m.end());
    in.insert(in.end(), {0x90, 0x40, 0x7F});
    h.feed(true, in, 1);
    CHECK_OUT(h, {{m, 1}, {{0x90, 0x40, 0x7F}, 1}});
  }
}

TEST_CASE(
    "R22 MTC quarter frame aborts the sysex, ignore_timing drops it (multi)",
    "[decoder][sysex][abort][ignore]")
{
  harness h{{.ignore_timing = true}};
  h.feed(true, {0xF0, 0x01, 0xF1, 0x23, 0x90, 0x40, 0x7F}, 1);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}});
}

TEST_CASE(
    "R23 an F7 left over from an aborted sysex is not a message", "[decoder][sysex][abort][stray]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01}, 1);
  h.feed(multi, {0xF6}, 2);
  h.feed(multi, {0xF7}, 3);
  CHECK_OUT(h, {{{0xF6}, 2}});
}

TEST_CASE("R24 stray F7 without F0 is not a message", "[decoder][stray]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF7}, 1);
  h.feed(multi, {0x90, 0x40, 0x7F}, 2);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 2}});
}

TEST_CASE(
    "R25 continuation chunk of an aborted sysex is not delivered as a message",
    "[decoder][sysex][abort][stray]")
{
  // ALSA seq / PipeWire input ports merge several sources: a note from B can
  // land between two sysex chunks of A.
  // Those are event APIs, decoded by on_bytes. In a byte stream, the same data bytes
  // follow a note on and are running status.
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01, 0x02}, 1);
  h.feed(multi, {0x90, 0x40, 0x7F}, 2);
  h.feed(multi, {0x03, 0x04, 0xF7}, 3);
  if (multi)
    CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 2}, {{0x90, 0x03, 0x04}, 3}});
  else
    CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 2}});
}

TEST_CASE("R26 data byte after F7 then a message in the same buffer", "[decoder][sysex][stray]")
{
  harness h;
  h.feed(true, {0xF0, 0x01, 0xF7, 0x05, 0x90, 0x40, 0x7F}, 1);
  CHECK_OUT(h, {{{0xF0, 0x01, 0xF7}, 1}, {{0x90, 0x40, 0x7F}, 1}});
}

TEST_CASE(
    "R27 sysex end then messages in the next buffer (CoreMIDI)", "[decoder][sysex][coremidi]")
{
  harness h;
  h.feed(true, {0xF0, 0x01, 0x02}, 1);
  h.feed(true, {0x03, 0xF7, 0x90, 0x40, 0x7F, 0xF8}, 2);
  CHECK_OUT(h, {{{0xF0, 0x01, 0x02, 0x03, 0xF7}, 2}, {{0x90, 0x40, 0x7F}, 2}, {{0xF8}, 2}});
}

TEST_CASE("R28 messages then a sysex in one buffer (multi)", "[decoder][sysex]")
{
  harness h;
  h.feed(true, {0x90, 0x40, 0x7F, 0xF0, 0x01, 0xF7, 0x80, 0x40, 0x00}, 1);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}, {{0xF0, 0x01, 0xF7}, 1}, {{0x80, 0x40, 0x00}, 1}});
}

// ---------------------------------------------------------------------------
// Messages cut across buffers, several messages per buffer, running status
// ---------------------------------------------------------------------------

TEST_CASE("R30 channel message split across multi buffers", "[decoder][split]")
{
  harness h;
  h.feed(true, {0x90, 0x40}, 1);
  h.feed(true, {0x7F}, 2);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 2}});
}

TEST_CASE("R31 song position split across multi buffers", "[decoder][split]")
{
  harness h;
  h.feed(true, {0xF2, 0x01}, 1);
  h.feed(true, {0x02}, 2);
  CHECK_OUT(h, {{{0xF2, 0x01, 0x02}, 2}});
}

TEST_CASE("R32 a message cut by the end of an on_bytes call is dropped", "[decoder][split]")
{
  // Event APIs never cut a short message: byte streams go through on_bytes_multi (R30).
  harness h;
  h.feed(false, {0x90, 0x40}, 1);
  h.feed(false, {0x7F}, 2);
  h.feed(false, {0x80, 0x40, 0x00}, 3);
  CHECK_OUT(h, {{{0x80, 0x40, 0x00}, 3}});
}

TEST_CASE("R33 several messages in one on_bytes call", "[decoder]")
{
  harness h;
  h.feed(false, {0x90, 0x40, 0x7F, 0x80, 0x40, 0x00}, 1);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}, {{0x80, 0x40, 0x00}, 1}});
}

TEST_CASE("R34 sysex then a message in one on_bytes call", "[decoder][sysex]")
{
  harness h;
  h.feed(false, {0xF0, 0x01, 0xF7, 0x90, 0x40, 0x7F}, 1);
  CHECK_OUT(h, {{{0xF0, 0x01, 0xF7}, 1}, {{0x90, 0x40, 0x7F}, 1}});
}

TEST_CASE("R35 sysex then two messages in one on_bytes call", "[decoder][sysex]")
{
  harness h;
  h.feed(false, {0xF0, 0x01, 0xF7, 0x90, 0x40, 0x7F, 0x80, 0x40, 0x00}, 1);
  CHECK_OUT(h, {{{0xF0, 0x01, 0xF7}, 1}, {{0x90, 0x40, 0x7F}, 1}, {{0x80, 0x40, 0x00}, 1}});
}

TEST_CASE("R36 running status", "[decoder][running_status]")
{
  SECTION("in one buffer")
  {
    harness h;
    h.feed(true, {0x90, 0x40, 0x7F, 0x41, 0x7F}, 1);
    CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}, {{0x90, 0x41, 0x7F}, 1}});
  }
  SECTION("across buffers, two-byte messages")
  {
    harness h;
    h.feed(true, {0xC0, 0x01, 0x02}, 1);
    h.feed(true, {0x03}, 2);
    CHECK_OUT(h, {{{0xC0, 0x01}, 1}, {{0xC0, 0x02}, 1}, {{0xC0, 0x03}, 2}});
  }
  SECTION("kept across real-time")
  {
    harness h{{.ignore_timing = false}};
    h.feed(true, {0xB0, 0x07, 0x10, 0xF8, 0x07, 0x20}, 1);
    CHECK_OUT(h, {{{0xB0, 0x07, 0x10}, 1}, {{0xF8}, 1}, {{0xB0, 0x07, 0x20}, 1}});
  }
  SECTION("cancelled by system common")
  {
    harness h;
    h.feed(true, {0x90, 0x40, 0x7F, 0xF6, 0x41, 0x7F}, 1);
    CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}, {{0xF6}, 1}});
  }
  SECTION("cancelled by sysex")
  {
    harness h;
    h.feed(true, {0x90, 0x40, 0x7F, 0xF0, 0x01, 0xF7, 0x41, 0x7F}, 1);
    CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}, {{0xF0, 0x01, 0xF7}, 1}});
  }
  SECTION("cancelled by a stray F7")
  {
    harness h;
    h.feed(true, {0x90, 0x40, 0x7F, 0xF7, 0x41, 0x7F}, 1);
    CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}});
  }
  SECTION("not applied by on_bytes, where trailing data bytes are padding")
  {
    harness h;
    h.feed(false, {0x90, 0x40, 0x7F, 0x41, 0x7F}, 1);
    h.feed(false, {0xC0, 0x05, 0x00}, 2);
    h.feed(false, {0x41, 0x7F}, 3);
    CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}, {{0xC0, 0x05}, 2}});
  }
}

TEST_CASE("R37 real-time inside a channel message", "[decoder][realtime]")
{
  PATHS();
  harness h{{.ignore_sensing = true}};
  h.feed(multi, {0x90, 0xF8, 0x40, 0xFE, 0x7F}, 1);
  h.feed(multi, {0xF2, 0x01, 0xFA, 0x02}, 2);
  CHECK_OUT(h, {{{0xF8}, 1}, {{0x90, 0x40, 0x7F}, 1}, {{0xFA}, 2}, {{0xF2, 0x01, 0x02}, 2}});
}

TEST_CASE(
    "R37b real-time inside a channel message cut across buffers (multi)",
    "[decoder][realtime][split]")
{
  harness h{{.ignore_timing = false}};
  h.feed(true, {0x90, 0x40}, 1);
  h.feed(true, {0xF8}, 2);
  h.feed(true, {0x7F}, 3);
  CHECK_OUT(h, {{{0xF8}, 2}, {{0x90, 0x40, 0x7F}, 3}});
}

TEST_CASE("R38 MTC at the end of a buffer with ignore_timing (multi)", "[decoder][ignore]")
{
  harness h{{.ignore_timing = true}};
  h.feed(true, {0x90, 0x40, 0x7F, 0xF1, 0x23}, 1);
  h.feed(true, {0xF8, 0xF1, 0x10, 0x80, 0x40, 0x00}, 2);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 1}, {{0x80, 0x40, 0x00}, 2}});
}

TEST_CASE("R39 a lone data byte is not a message", "[decoder][stray]")
{
  PATHS();
  harness h;
  h.feed(multi, {0x40}, 1);
  CHECK(h.out.empty());
}

// ---------------------------------------------------------------------------
// State handling
// ---------------------------------------------------------------------------

TEST_CASE("R40 reset() in the middle of a sysex", "[decoder][state]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01, 0x02}, 1);
  h.sm->reset();
  h.feed(multi, {0x90, 0x40, 0x7F}, 2);
  h.feed(multi, {0xF0, 0x03, 0xF7}, 3);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 2}, {{0xF0, 0x03, 0xF7}, 3}});
}

TEST_CASE("R41 empty buffers in both states", "[decoder][state]")
{
  PATHS();
  harness h;
  h.feed(multi, {}, 1);
  h.feed(multi, {0xF0, 0x01}, 2);
  h.feed(multi, {}, 3);
  h.feed(multi, {0xF7}, 4);
  CHECK_OUT(h, {{{0xF0, 0x01, 0xF7}, 4}});
}

TEST_CASE(
    "R42 a callback that copies instead of moving does not pollute the next message",
    "[decoder][state]")
{
  PATHS();
  libremidi::input_configuration conf;
  std::vector<bytes_t> out;
  conf.on_message = [&](libremidi::message&& m) {
    out.emplace_back(m.bytes.begin(), m.bytes.end()); // no move
  };
  conf.ignore_sysex = conf.ignore_timing = conf.ignore_sensing = false;
  libremidi::midi1::input_state_machine sm{conf};
  auto feed = [&](bytes_t b) { multi ? sm.on_bytes_multi(b, 0) : sm.on_bytes(b, 0); };
  feed({0xF0, 0x01, 0xF8, 0x02});
  feed({0x03, 0xF7});
  feed({0xF0, 0x04, 0xF7});
  feed({0x90, 0x40, 0x7F});
  REQUIRE(out.size() == 4);
  CHECK(out[0] == bytes_t{0xF8});
  CHECK(out[1] == bytes_t{0xF0, 0x01, 0x02, 0x03, 0xF7});
  CHECK(out[2] == bytes_t{0xF0, 0x04, 0xF7});
  CHECK(out[3] == bytes_t{0x90, 0x40, 0x7F});
}

TEST_CASE(
    "R43 the decoder's public message buffer is empty after a sysex is delivered",
    "[decoder][state]")
{
  PATHS();
  harness h;
  h.feed(multi, {0xF0, 0x01, 0xF7}, 1);
  CHECK(h.sm->message.bytes.empty());
}

TEST_CASE("R44 timestamps of delivered messages never go backwards", "[decoder][timestamps]")
{
  // A real-time byte inside a multi-buffer sysex is delivered with its own
  // buffer's time, then the sysex with the time of the buffer holding its F7.
  PATHS();
  harness h{{.ignore_timing = false}};
  h.feed(multi, {0xF0, 0x01}, 100);
  h.feed(multi, {0x02, 0xF8, 0x03}, 200);
  h.feed(multi, {0xF7}, 300);
  CHECK_OUT(h, {{{0xF8}, 200}, {{0xF0, 0x01, 0x02, 0x03, 0xF7}, 300}});
}

TEST_CASE(
    "R45 ignore_sysex keeps the decoder's buffer small during a long sysex",
    "[decoder][sysex][ignore][memory]")
{
  PATHS();
  harness h{{.ignore_sysex = true}};
  h.feed(multi, {0xF0}, 1);
  for (int i = 0; i < 1000; i++)
    h.feed(multi, bytes_t(1024, 0x11), 2);
  CHECK(h.sm->message.bytes.size() <= 1);
}

TEST_CASE(
    "R46 sysex longer than a slim message's capacity does not throw", "[decoder][sysex][slim]")
{
  PATHS();
  harness h;
  bytes_t sx{0xF0};
  sx.insert(sx.end(), 300, 0x11);
  sx.push_back(0xF7);
  CHECK_NOTHROW(h.feed(multi, sx, 1));
  CHECK_NOTHROW(h.feed(multi, {0x90, 0x40, 0x7F}, 2));
  REQUIRE(!h.out.empty());
  CHECK(h.out.back() == out_msg{{0x90, 0x40, 0x7F}, 2});
}

TEST_CASE(
    "R47 a callback that feeds the decoder again (re-entrancy) while a sysex is open",
    "[decoder][state][reentrancy]")
{
  PATHS();
  libremidi::input_configuration conf;
  conf.ignore_sysex = conf.ignore_timing = conf.ignore_sensing = false;
  std::vector<bytes_t> out;
  std::optional<libremidi::midi1::input_state_machine> sm;
  bool once = false;
  conf.on_message = [&](libremidi::message&& m) {
    out.emplace_back(m.bytes.begin(), m.bytes.end());
    if (m.bytes.size() == 1 && m.bytes[0] == 0xF8 && !once)
    {
      once = true;
      const uint8_t rt[] = {0xFA};
      sm->on_bytes(rt, 0);
    }
  };
  sm.emplace(conf);
  bytes_t a{0xF0, 0x01, 0xF8, 0x02, 0xF7};
  multi ? sm->on_bytes_multi(a, 0) : sm->on_bytes(a, 0);
  CHECK(out == std::vector<bytes_t>{{0xF8}, {0xFA}, {0xF0, 0x01, 0x02, 0xF7}});
}

TEST_CASE(
    "R48 a sysex longer than sysex_max_size is dropped and reported", "[decoder][sysex][limit]")
{
  PATHS();
  libremidi::input_configuration conf;
  std::vector<bytes_t> out;
  int warnings = 0;
  conf.on_message
      = [&](libremidi::message&& m) { out.emplace_back(m.bytes.begin(), m.bytes.end()); };
  conf.on_warning = [&](std::string_view, const libremidi::source_location&) { warnings++; };
  conf.ignore_sysex = conf.ignore_timing = conf.ignore_sensing = false;
  conf.sysex_max_size = 8;
  libremidi::midi1::input_state_machine sm{conf};
  auto feed = [&](bytes_t b) { multi ? sm.on_bytes_multi(b, 0) : sm.on_bytes(b, 0); };

  // Exactly the maximum size, F0 and F7 included
  feed({0xF0, 1, 2, 3, 4, 5, 6, 0xF7});
  // One byte more, cut across buffers, with real-time inside
  feed({0xF0, 1, 2, 3});
  feed({4, 0xF8, 5, 6, 7});
  feed({0xF7});
  feed({0x90, 0x40, 0x7F});
  feed({0xF0, 9, 0xF7});

  CHECK(
      out
      == std::vector<bytes_t>{
          {0xF0, 1, 2, 3, 4, 5, 6, 0xF7}, {0xF8}, {0x90, 0x40, 0x7F}, {0xF0, 9, 0xF7}});
  CHECK(warnings == 1);
  CHECK(sm.message.bytes.empty());
}

TEST_CASE(
    "R49 a never-terminated sysex does not grow past sysex_max_size",
    "[decoder][sysex][limit][memory]")
{
  PATHS();
  harness h;
  h.conf.sysex_max_size = 4096;
  int warnings = 0;
  h.conf.on_warning = [&](std::string_view, const libremidi::source_location&) { warnings++; };
  h.feed(multi, {0xF0}, 1);
  for (int i = 0; i < 1000; i++)
    h.feed(multi, bytes_t(1024, 0x11), 2);
  CHECK(h.sm->message.bytes.size() <= 4096);
  CHECK(warnings == 1);
  h.feed(multi, {0xF7, 0x90, 0x40, 0x7F}, 3);
  CHECK_OUT(h, {{{0x90, 0x40, 0x7F}, 3}});
}

TEST_CASE("R50 sysex_max_size 0 sets no limit", "[decoder][sysex][limit]")
{
  PATHS();
  harness h;
  h.conf.sysex_max_size = 0;
  bytes_t sx{0xF0};
  sx.insert(sx.end(), 100000, 0x11);
  sx.push_back(0xF7);
  h.feed(multi, sx, 1);
  if (h.sm->message.bytes.max_size() >= sx.size())
    CHECK_OUT(h, {{sx, 1}});
  else
    CHECK(h.out.empty());
}

TEST_CASE(
    "R51 a callback that throws leaves the decoder in a usable state",
    "[decoder][state][exceptions]")
{
  PATHS();
  libremidi::input_configuration conf;
  std::vector<bytes_t> out;
  bool armed = true;
  conf.on_message = [&](libremidi::message&& m) {
    if (armed)
    {
      armed = false;
      throw std::runtime_error("callback");
    }
    out.emplace_back(m.bytes.begin(), m.bytes.end());
  };
  conf.ignore_sysex = conf.ignore_timing = conf.ignore_sensing = false;
  libremidi::midi1::input_state_machine sm{conf};
  auto feed = [&](bytes_t b) { multi ? sm.on_bytes_multi(b, 0) : sm.on_bytes(b, 0); };

  SECTION("on a sysex")
  {
    feed({0xF0, 0x01});
    CHECK_THROWS(feed({0x02, 0xF7}));
    CHECK(sm.message.bytes.empty());
    feed({0x05, 0x06});
    feed({0xF0, 0x03, 0xF7});
    feed({0x90, 0x40, 0x7F});
    CHECK(out == std::vector<bytes_t>{{0xF0, 0x03, 0xF7}, {0x90, 0x40, 0x7F}});
  }
  SECTION("on a real-time byte inside a sysex")
  {
    CHECK_THROWS(feed({0xF0, 0x01, 0xF8, 0x02}));
    // The rest of the buffer after the throw is lost; the sysex goes on.
    feed({0x03, 0xF7});
    CHECK(out == std::vector<bytes_t>{{0xF0, 0x01, 0x03, 0xF7}});
  }
  SECTION("on a channel message")
  {
    CHECK_THROWS(feed({0x90, 0x40, 0x7F}));
    feed({0x80, 0x40, 0x00});
    CHECK(out == std::vector<bytes_t>{{0x80, 0x40, 0x00}});
  }
}

// ---------------------------------------------------------------------------
// Property tests: random valid streams, random splitting
// ---------------------------------------------------------------------------

namespace
{
struct unit
{
  enum kind_t
  {
    channel,
    common,
    realtime,
    sysex,
    sysex_aborted
  } kind;
  // The message it decodes to
  bytes_t bytes;
  // How it appears in the stream: status omitted by running status, real-time inside
  bytes_t encoded{};
};

struct stream
{
  std::vector<unit> units;
  bytes_t bytes;
  std::vector<std::size_t> unit_begin; // stream offset of each unit
  std::vector<bool> inside_sysex;      // per byte, strictly after F0 of a sysex unit
  std::vector<bool> rt_inside_sysex;   // per byte
};

struct expectation
{
  // Offset whose processing emits it: its last byte, whose buffer stamps it
  std::size_t emit_pos;
  bytes_t b;
};

uint8_t data(std::mt19937& g)
{
  return std::uniform_int_distribution<int>(0, 0x7F)(g);
}

uint8_t rt_byte(std::mt19937& g, bool with_undefined)
{
  static const uint8_t def[] = {0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF};
  static const uint8_t all[] = {0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};
  if (with_undefined)
    return all[std::uniform_int_distribution<int>(0, 7)(g)];
  return def[std::uniform_int_distribution<int>(0, 5)(g)];
}

unit random_non_rt_status_unit(std::mt19937& g, bool allow_sysex)
{
  int k = std::uniform_int_distribution<int>(0, allow_sysex ? 9 : 7)(g);
  if (k <= 4)
  {
    uint8_t st = 0x80 | (std::uniform_int_distribution<int>(0, 6)(g) << 4)
                 | std::uniform_int_distribution<int>(0, 15)(g);
    const int hi = st & 0xF0;
    if (hi == 0xC0 || hi == 0xD0)
      return {unit::channel, {st, data(g)}};
    return {unit::channel, {st, data(g), data(g)}};
  }
  if (k <= 7)
  {
    switch (std::uniform_int_distribution<int>(0, 5)(g))
    {
      case 0:
        return {unit::common, {0xF1, data(g)}};
      case 1:
        return {unit::common, {0xF2, data(g), data(g)}};
      case 2:
        return {unit::common, {0xF3, data(g)}};
      case 3:
        return {unit::common, {0xF4}};
      case 4:
        return {unit::common, {0xF5}};
      default:
        return {unit::common, {0xF6}};
    }
  }
  return {unit::sysex, {}}; // filled by caller
}

unit random_sysex(std::mt19937& g, bool aborted, bool rt_inside, bool undefined_rt)
{
  unit u{aborted ? unit::sysex_aborted : unit::sysex, {0xF0}};
  const int len = std::uniform_int_distribution<int>(0, 3)(g) == 0
                      ? std::uniform_int_distribution<int>(0, 600)(g)
                      : std::uniform_int_distribution<int>(0, 12)(g);
  for (int i = 0; i < len; i++)
  {
    if (rt_inside && std::uniform_int_distribution<int>(0, 7)(g) == 0)
      u.bytes.push_back(rt_byte(g, undefined_rt));
    u.bytes.push_back(data(g));
  }
  if (rt_inside && std::uniform_int_distribution<int>(0, 5)(g) == 0)
    u.bytes.push_back(rt_byte(g, undefined_rt));
  if (!aborted)
    u.bytes.push_back(0xF7);
  return u;
}

struct stream_options
{
  bool rt_inside{};
  bool aborts{};
  bool undefined_rt{};
  // Byte stream only: status bytes omitted, real-time bytes inside short messages
  bool running_status{};
  bool rt_in_short{};
};

stream random_stream(std::mt19937& g, int n_units, stream_options o)
{
  const bool rt_inside = o.rt_inside, aborts = o.aborts, undefined_rt = o.undefined_rt;
  stream s;
  for (int i = 0; i < n_units; i++)
  {
    const int k = std::uniform_int_distribution<int>(0, 9)(g);
    if (k <= 2)
      s.units.push_back({unit::realtime, {rt_byte(g, undefined_rt)}});
    else if (k <= 5)
      s.units.push_back(random_sysex(
          g, aborts && (std::uniform_int_distribution<int>(0, 3)(g) == 0), rt_inside,
          undefined_rt));
    else
    {
      auto u = random_non_rt_status_unit(g, false);
      s.units.push_back(u);
    }
  }
  // An aborted sysex must be followed by a non real-time status.
  for (std::size_t i = 0; i < s.units.size(); i++)
  {
    if (s.units[i].kind == unit::sysex_aborted)
    {
      if (i + 1 == s.units.size() || s.units[i + 1].kind == unit::realtime)
        s.units.insert(s.units.begin() + i + 1, random_non_rt_status_unit(g, false));
    }
  }
  uint8_t running = 0;
  for (auto& u : s.units)
  {
    u.encoded = u.bytes;
    switch (u.kind)
    {
      case unit::channel:
        if (o.running_status && running == u.bytes[0] && std::bernoulli_distribution(0.6)(g))
          u.encoded.erase(u.encoded.begin());
        running = u.bytes[0];
        break;
      case unit::realtime:
        break;
      default:
        running = 0;
        break;
    }
    if (o.rt_in_short && (u.kind == unit::channel || u.kind == unit::common)
        && u.encoded.size() >= 2 && std::bernoulli_distribution(0.4)(g))
    {
      const auto pos = std::uniform_int_distribution<std::size_t>(1, u.encoded.size() - 1)(g);
      u.encoded.insert(u.encoded.begin() + pos, rt_byte(g, undefined_rt));
    }

    s.unit_begin.push_back(s.bytes.size());
    for (std::size_t j = 0; j < u.encoded.size(); j++)
    {
      const bool is_sx = u.kind == unit::sysex || u.kind == unit::sysex_aborted;
      s.inside_sysex.push_back(is_sx && j > 0);
      s.rt_inside_sysex.push_back(is_sx && j > 0 && u.encoded[j] >= 0xF8);
      s.bytes.push_back(u.encoded[j]);
    }
  }
  return s;
}

bool rt_kept(uint8_t b, flags f)
{
  if (b == 0xF8 && f.ignore_timing)
    return false;
  if (b == 0xFE && f.ignore_sensing)
    return false;
  return true;
}

std::vector<expectation> expected_of(const stream& s, flags f)
{
  std::vector<expectation> e;
  for (std::size_t i = 0; i < s.units.size(); i++)
  {
    const auto& u = s.units[i];
    const auto at = s.unit_begin[i];
    const auto last = at + u.encoded.size() - 1;
    // Real-time bytes inside another message
    if (u.kind != unit::realtime)
      for (std::size_t j = 1; j < u.encoded.size(); j++)
        if (u.encoded[j] >= 0xF8 && rt_kept(u.encoded[j], f))
          e.push_back({at + j, {u.encoded[j]}});

    switch (u.kind)
    {
      case unit::channel:
        e.push_back({last, u.bytes});
        break;
      case unit::common:
        if (u.bytes[0] == 0xF1 && f.ignore_timing)
          break;
        e.push_back({last, u.bytes});
        break;
      case unit::realtime:
        if (rt_kept(u.bytes[0], f))
          e.push_back({at, u.bytes});
        break;
      case unit::sysex:
      case unit::sysex_aborted: {
        bytes_t body;
        for (auto b : u.encoded)
          if (b < 0xF8)
            body.push_back(b);
        // Longer than a slim message holds: dropped
        const std::size_t capacity = std::min<std::size_t>(
            libremidi::message{}.bytes.max_size(),
            libremidi::input_configuration{}.sysex_max_size);
        if (u.kind == unit::sysex && !f.ignore_sysex && body.size() <= capacity)
          e.push_back({last, body});
        break;
      }
    }
  }
  std::stable_sort(e.begin(), e.end(), [](auto& a, auto& b) { return a.emit_pos < b.emit_pos; });
  return e;
}

enum class split_mode
{
  // CoreMIDI-like: packets end on message boundaries, sysex may be cut anywhere.
  boundaries_and_sysex,
  // One message per call; sysex chunked anywhere (JACK, PipeWire, WinMM, ALSA seq).
  one_message_per_call,
  // Same, and every real-time byte inside a sysex comes in its own call.
  one_message_per_call_rt_isolated,
  // Arbitrary byte chunks (raw byte streams: rawio, Android, ALSA rawmidi).
  anywhere
};

std::vector<std::size_t> cuts_for(const stream& s, split_mode m, std::mt19937& g)
{
  std::vector<bool> cut(s.bytes.size() + 1, false);
  std::bernoulli_distribution coin(0.3);
  std::bernoulli_distribution sx_coin(0.15);
  for (std::size_t i = 1; i < s.units.size(); i++)
  {
    const auto at = s.unit_begin[i];
    if (m == split_mode::one_message_per_call || m == split_mode::one_message_per_call_rt_isolated)
      cut[at] = true;
    else if (coin(g))
      cut[at] = true;
  }
  for (std::size_t p = 1; p < s.bytes.size(); p++)
  {
    if (m == split_mode::anywhere && coin(g))
      cut[p] = true;
    if (s.inside_sysex[p] && sx_coin(g))
      cut[p] = true;
    if (m == split_mode::one_message_per_call_rt_isolated && s.rt_inside_sysex[p])
    {
      cut[p] = true;
      cut[p + 1] = true;
    }
  }
  std::vector<std::size_t> res{0};
  for (std::size_t p = 1; p < s.bytes.size(); p++)
    if (cut[p])
      res.push_back(p);
  res.push_back(s.bytes.size());
  return res;
}

void run_property(
    bool multi, split_mode mode, stream_options o, bool random_flags, int iterations,
    uint32_t seed)
{
  std::mt19937 g{seed};
  int failures = 0;
  for (int it = 0; it < iterations && failures < 3; it++)
  {
    flags f{};
    if (random_flags)
    {
      f.ignore_sysex = std::bernoulli_distribution(0.3)(g);
      f.ignore_timing = std::bernoulli_distribution(0.5)(g);
      f.ignore_sensing = std::bernoulli_distribution(0.5)(g);
    }
    auto s = random_stream(g, std::uniform_int_distribution<int>(1, 25)(g), o);
    auto cuts = cuts_for(s, mode, g);

    // Timestamp of each byte is the index of its chunk.
    std::vector<int64_t> stamp(s.bytes.size());
    harness h{f};
    for (std::size_t c = 0; c + 1 < cuts.size(); c++)
    {
      for (auto p = cuts[c]; p < cuts[c + 1]; p++)
        stamp[p] = (int64_t)c;
      h.feed(multi, bytes_t(s.bytes.begin() + cuts[c], s.bytes.begin() + cuts[c + 1]), (int64_t)c);
    }
    std::vector<out_msg> exp;
    for (auto& e : expected_of(s, f))
      exp.push_back({e.b, stamp[e.emit_pos]});

    if (h.out != exp)
    {
      failures++;
      std::ostringstream chunks;
      for (std::size_t c = 0; c + 1 < cuts.size(); c++)
        chunks << "  chunk " << c << ": "
               << hex(bytes_t(s.bytes.begin() + cuts[c], s.bytes.begin() + cuts[c + 1])) << "\n";
      INFO(
          "iteration " << it << " flags sysex/timing/sensing " << f.ignore_sysex << f.ignore_timing
                       << f.ignore_sensing << "\n"
                       << chunks.str() << "got: " << dump(h.out) << "expected: " << dump(exp));
      CHECK(h.out == exp);
    }
  }
  CHECK(failures == 0);
}
}

TEST_CASE("P01 multi, CoreMIDI-like packets, no real-time in sysex", "[decoder][property]")
{
  run_property(
      true, split_mode::boundaries_and_sysex,
      {.rt_inside = false, .aborts = false, .undefined_rt = false}, false, 3000, 1);
}
TEST_CASE("P02 multi, CoreMIDI-like packets, real-time inside sysex", "[decoder][property]")
{
  run_property(
      true, split_mode::boundaries_and_sysex,
      {.rt_inside = true, .aborts = false, .undefined_rt = false}, false, 3000, 2);
}
TEST_CASE(
    "P03 multi, CoreMIDI-like packets, real-time inside + aborted sysex + undefined RT",
    "[decoder][property]")
{
  run_property(
      true, split_mode::boundaries_and_sysex,
      {.rt_inside = true, .aborts = true, .undefined_rt = true}, false, 3000, 3);
}
TEST_CASE(
    "P04 multi, CoreMIDI-like packets, everything + random ignore flags",
    "[decoder][property][ignore]")
{
  run_property(
      true, split_mode::boundaries_and_sysex,
      {.rt_inside = true, .aborts = true, .undefined_rt = true}, true, 5000, 4);
}
TEST_CASE(
    "P05 on_bytes, one message per call, sysex chunked, no real-time in sysex",
    "[decoder][property]")
{
  run_property(
      false, split_mode::one_message_per_call,
      {.rt_inside = false, .aborts = false, .undefined_rt = false}, false, 3000, 5);
}
TEST_CASE(
    "P06 on_bytes, one message per call, real-time inside sysex chunks", "[decoder][property]")
{
  run_property(
      false, split_mode::one_message_per_call,
      {.rt_inside = true, .aborts = false, .undefined_rt = false}, false, 3000, 6);
}
TEST_CASE("P07 on_bytes, one message per call, real-time in their own call", "[decoder][property]")
{
  run_property(
      false, split_mode::one_message_per_call_rt_isolated,
      {.rt_inside = true, .aborts = false, .undefined_rt = false}, false, 3000, 7);
}
TEST_CASE("P08 on_bytes, everything + aborts + random ignore flags", "[decoder][property][ignore]")
{
  run_property(
      false, split_mode::one_message_per_call,
      {.rt_inside = true, .aborts = true, .undefined_rt = true}, true, 5000, 8);
}
TEST_CASE(
    "P09 on_bytes, RT isolated, everything + random ignore flags", "[decoder][property][ignore]")
{
  run_property(
      false, split_mode::one_message_per_call_rt_isolated,
      {.rt_inside = true, .aborts = true, .undefined_rt = true}, true, 5000, 9);
}
TEST_CASE(
    "P10 multi, arbitrary byte chunks (rawio, Android, ALSA rawmidi)",
    "[decoder][property][anywhere]")
{
  run_property(
      true, split_mode::anywhere, {.rt_inside = false, .aborts = false, .undefined_rt = false},
      false, 2000, 10);
}
TEST_CASE(
    "P11 multi, arbitrary byte chunks, running status and real-time anywhere",
    "[decoder][property][anywhere][running_status]")
{
  run_property(
      true, split_mode::anywhere,
      {.rt_inside = true,
       .aborts = true,
       .undefined_rt = true,
       .running_status = true,
       .rt_in_short = true},
      true, 5000, 11);
}

// ---------------------------------------------------------------------------
// Fuzz: random bytes, random chunks, random flags. Run under ASan/UBSan.
// ---------------------------------------------------------------------------

namespace
{
std::size_t expected_length(uint8_t status)
{
  switch (status & 0xF0)
  {
    case 0xC0:
    case 0xD0:
      return 2;
    case 0xF0:
      return status == 0xF1 || status == 0xF3 ? 2 : status == 0xF2 ? 3 : 1;
    default:
      return 3;
  }
}

void fuzz(bool multi, uint32_t seed, int iterations, bool check_invariants)
{
  std::mt19937 g{seed};
  int bad = 0;
  for (int it = 0; it < iterations; it++)
  {
    flags f{
        std::bernoulli_distribution(0.3)(g), std::bernoulli_distribution(0.3)(g),
        std::bernoulli_distribution(0.3)(g)};
    harness h{f};
    const int n_chunks = std::uniform_int_distribution<int>(1, 8)(g);
    for (int c = 0; c < n_chunks; c++)
    {
      bytes_t b(std::uniform_int_distribution<int>(0, 24)(g));
      for (auto& x : b)
        x = std::bernoulli_distribution(0.35)(g) ? (uint8_t)(0x80 | data(g)) : data(g);
      h.feed(multi, b, c);
    }
    if (!check_invariants)
      continue;
    for (auto& m : h.out)
    {
      bool ok = !m.b.empty();
      if (ok && m.b[0] == 0xF0)
      {
        ok = m.b.size() >= 2 && m.b.back() == 0xF7;
        for (std::size_t i = 1; ok && i + 1 < m.b.size(); i++)
          ok = !(m.b[i] & 0x80);
      }
      else if (ok && m.b[0] >= 0xF8)
        ok = m.b.size() == 1;
      else if (ok)
        ok = (m.b[0] & 0x80) && m.b[0] != 0xF7 && m.b.size() == expected_length(m.b[0])
             && std::none_of(m.b.begin() + 1, m.b.end(), [](uint8_t b) { return b & 0x80; });
      if (!ok && bad++ < 5)
      {
        INFO("iteration " << it << " bad message " << hex(m.b));
        CHECK(ok);
      }
    }
  }
  CHECK(bad == 0);
}
}

TEST_CASE("F01 fuzz on_bytes, no crash", "[decoder][fuzz]")
{
  fuzz(false, 101, 200000, false);
}
TEST_CASE("F02 fuzz on_bytes_multi, no crash", "[decoder][fuzz]")
{
  fuzz(true, 102, 200000, false);
}
TEST_CASE("F03 fuzz on_bytes, well-formed output", "[decoder][fuzz][invariants]")
{
  fuzz(false, 103, 20000, true);
}
TEST_CASE("F04 fuzz on_bytes_multi, well-formed output", "[decoder][fuzz][invariants]")
{
  fuzz(true, 104, 20000, true);
}

// ---------------------------------------------------------------------------
// Throughput
// ---------------------------------------------------------------------------

TEST_CASE("B01 throughput of a 16 MiB sysex in 256-byte chunks", "[decoder][bench]")
{
  for (bool multi : {false, true})
  {
    libremidi::input_configuration conf;
    std::size_t got = 0;
    conf.on_message = [&](libremidi::message&& m) { got += m.size(); };
    conf.ignore_sysex = conf.ignore_timing = conf.ignore_sensing = false;
    conf.sysex_max_size = 0;
    libremidi::midi1::input_state_machine sm{conf};
    const std::size_t total = 16u << 20;
    bytes_t chunk(256, 0x55);
    auto t0 = std::chrono::steady_clock::now();
    bytes_t first = chunk;
    first[0] = 0xF0;
    multi ? sm.on_bytes_multi(first, 0) : sm.on_bytes(first, 0);
    for (std::size_t i = 256; i + 256 < total; i += 256)
      multi ? sm.on_bytes_multi(chunk, 0) : sm.on_bytes(chunk, 0);
    bytes_t last = chunk;
    last.back() = 0xF7;
    multi ? sm.on_bytes_multi(last, 0) : sm.on_bytes(last, 0);
    auto t1 = std::chrono::steady_clock::now();
    std::printf(
        "B01 %s: %.1f ms for %zu bytes (delivered %zu)\n", multi ? "multi" : "on_bytes",
        std::chrono::duration<double, std::milli>(t1 - t0).count(), total, got);
    CHECK(got > 0);
  }
}
