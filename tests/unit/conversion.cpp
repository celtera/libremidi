#include "../include_catch.hpp"

#include <libremidi/detail/conversion.hpp>
#include <libremidi/libremidi.hpp>

#include <span>
#include <iomanip>

namespace Catch
{
template <>
struct StringMaker<stdx::error>
{
  static std::string convert(stdx::error const& value)
  {
    const auto& msg = value.message();
    return std::string(msg.begin(), msg.end());
  }
};
}

static std::string hex_dump(const std::span<const uint8_t>& data)
{
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (auto b : data)
  {
    ss << std::setw(2) << static_cast<int>(b) << " ";
  }
  return ss.str();
}

TEST_CASE("Roundtrip MIDI 1 -> MIDI 2 -> MIDI 1", "[midi_roundtrip]")
{
  libremidi::midi1_to_midi2 m1_to_m2;
  libremidi::midi2_to_midi1 m2_to_m1;
  struct RoundTripTestCase
  {
    std::string name;
    std::vector<uint8_t> midi1_bytes;
  };
  std::vector<RoundTripTestCase> tests = {

      // // --- System Exclusive ---
      // 1. Short SysEx (fits in a single 64-bit UMP packet payload)
      // 0xF0 + ID + data + 0xF7. Total 6 bytes.
      {"SysEx Short", {0xF0, 0x7E, 0x00, 0x01, 0x02, 0xF7}},

      // 2. Long SysEx (Requires multiple UMP packets)
      // Payload > 6 bytes forces conversion to Start/Continue/End UMP packets.
      // F0 + 8 bytes data + F7 = 10 bytes total.
      {"SysEx Long", {0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xF7}},

      // --- System Common Messages ---
      {"Song Select", {0xF3, 0x05}},             // Song 5
      {"MTC Quarter Frame", {0xF1, 0x20}},       // Type 1, Value 0
      {"Song Position Ptr", {0xF2, 0x00, 0x01}}, // LSB 00, MSB 01
      // 0xF4, 0xF5 are undefined
      {"Tune Request", {0xF6}},

      // --- System Real-Time Messages ---
      {"Timing Clock", {0xF8}},
      {"Start", {0xFA}},
      {"Continue", {0xFB}},
      {"Stop", {0xFC}},
      // 0xF9, 0xFD are undefined
      {"Active Sensing", {0xFE}},
      {"Reset", {0xFF}},

      // --- Channel Voice Messages ---
      {"Note Off", {0x80, 0x3C, 0x00}},          // Ch 1, Note 60, Vel 0
      {"Note On", {0x90, 0x3C, 0x64}},           // Ch 1, Note 60, Vel 100
      {"Poly Key Pressure", {0xA1, 0x3C, 0x7F}}, // Ch 2, Note 60, Val 127
      {"Control Change", {0xB2, 0x07, 0x64}},    // Ch 3, CC 7, Val 100
      {"Program Change", {0xC3, 0x0A}},          // Ch 4, Prog 10
      {"Channel Pressure", {0xD4, 0x40}},        // Ch 5, Val 64
      {"Pitch Bend", {0xE5, 0x00, 0x40}},        // Ch 6, Center (LSB 00, MSB 40)

  };

  for (const auto& test : tests)
  {
    DYNAMIC_SECTION("Test Case: " << test.name)
    {

      // --- Step 1: MIDI 1 to MIDI 2 ---
      INFO("MIDI 1 -> MIDI 2");
      std::vector<uint32_t> ump_data;
      bool m2_ok = false;

      auto err1 = m1_to_m2.convert(
          test.midi1_bytes.data(), test.midi1_bytes.size(), 0,
          [&](uint32_t* ump, std::size_t sz, int64_t ts) {
        m2_ok = true;
        ump_data.insert(ump_data.end(), ump, ump + sz);
        return stdx::error{};
      });

      REQUIRE(err1 == stdx::error{});
      REQUIRE(m2_ok);
      REQUIRE(!ump_data.empty());

      INFO("MIDI 2 -> MIDI 1: success!");
      auto dmp
          = hex_dump(std::span<const uint8_t>((uint8_t*)ump_data.data(), ump_data.size() * 4));
      INFO(dmp);
      // --- Step 2: MIDI 2 to MIDI 1 ---
      INFO("MIDI 2 -> MIDI 1");
      std::vector<uint8_t> result_bytes;
      bool m1_ok = false;

      auto err2 = m2_to_m1.convert(
          ump_data.data(), ump_data.size(), 0, [&](uint8_t* midi, std::size_t sz, int64_t ts) {
        m1_ok = true;
        result_bytes.insert(result_bytes.end(), midi, midi + sz);
        return stdx::error{};
      });

      REQUIRE(err2 == stdx::error{});

      // Note: m1_ok might be false if the UMP was purely utility/metadata
      // and produced no MIDI 1 bytes, but for these test cases,
      // we expect output.
      REQUIRE(m1_ok);

      // --- Step 3: Verify Roundtrip ---

      // If the sizes differ, Catch2 will show the breakdown
      if (result_bytes.size() != test.midi1_bytes.size())
      {
        UNSCOPED_INFO(
            "Size mismatch! Expected " << test.midi1_bytes.size() << " but got "
                                       << result_bytes.size());
      }

      // Verify content
      bool content_match = (result_bytes == test.midi1_bytes);
      if (!content_match)
      {
        UNSCOPED_INFO("Input : " << hex_dump(test.midi1_bytes));
        UNSCOPED_INFO("Output: " << hex_dump(result_bytes));
      }

      REQUIRE(result_bytes == test.midi1_bytes);
    }
  }
}

TEST_CASE("convert midi1 to midi2", "[midi_in]")
{
  libremidi::midi1_to_midi2 m1;
  GIVEN("Correct input")
  {
    bool ok = false;
    auto msg = libremidi::channel_events::control_change(3, 35, 100);

    std::vector<uint32_t> res;
    auto err
        = m1.convert(msg.bytes.data(), msg.size(), 0, [&](uint32_t* ump, std::size_t sz, int64_t) {
      ok = true;
      res.assign(ump, ump + sz);
      return stdx::error{};
    });

    THEN("Correct conversion")
    {
      REQUIRE(err == stdx::error{});
      REQUIRE(ok);
      auto expected = cmidi2_ump_midi2_cc(0, 2, 35, 100 << 25);
      union
      {
        uint32_t u[2];
        int64_t i;
      } e{.i = expected};

      REQUIRE(res.size() == 2);
      REQUIRE(e.u[1] == res[0]);
      REQUIRE(e.u[0] == res[1]);
    }
  }

  GIVEN("Wrong input")
  {
    bool ok = false;
    unsigned char arr[1]{156};
    auto err = m1.convert(arr, 1, 0, [&](uint32_t*, std::size_t, int64_t) {
      ok = true;
      return stdx::error{};
    });
    THEN("We get an error")
    {
      REQUIRE(err != stdx::error{});
      REQUIRE(ok == false);
    }
  }
}

TEST_CASE("convert midi2 to midi1", "[midi_in]")
{
  libremidi::midi2_to_midi1 m1;
  GIVEN("Correct input")
  {
    bool ok = false;
    auto msg = cmidi2_ump_midi2_cc(0, 2, 35, 100 << 25);
    union
    {
      uint32_t u[2];
      int64_t i;
    } e{.i = msg};
    using namespace std;
    swap(e.u[0], e.u[1]);

    std::vector<uint8_t> res;
    auto err = m1.convert(e.u, 2, 0, [&](uint8_t* midi, std::size_t sz, int64_t) {
      ok = true;
      res.assign(midi, midi + sz);
      return stdx::error{};
    });

    THEN("Correct conversion")
    {
      REQUIRE(err == stdx::error{});
      REQUIRE(ok);
      auto msg = libremidi::channel_events::control_change(3, 35, 100);

      REQUIRE(res.size() == 3);
      REQUIRE(res == std::vector<uint8_t>(msg.begin(), msg.end()));
    }
  }

  GIVEN("Wrong input")
  {
    bool ok = false;
    uint32_t res = 0xFFFFFFFF;
    auto err = m1.convert(&res, 1, 0, [&](uint8_t*, std::size_t, int64_t) {
      ok = true;
      return stdx::error{};
    });
    THEN("We get an error")
    {
      REQUIRE(err != stdx::error{});
      REQUIRE(ok == false);
    }
  }
}

TEST_CASE("MIDI 1 MTC Quarter Frame Sequence -> MIDI 2 UMP", "[midi_conversion][mtc]")
{
  libremidi::midi1_to_midi2 m1_to_m2;

  // We will construct a sequence of 8 Quarter Frame messages
  // representing the timecode: 01:02:03:04 (HH:MM:SS:FF)
  //
  // Breakdown:
  // Frames  : 04 -> LSB=4 (Msg 0), MSB=0 (Msg 1)
  // Seconds : 03 -> LSB=3 (Msg 2), MSB=0 (Msg 3)
  // Minutes : 02 -> LSB=2 (Msg 4), MSB=0 (Msg 5)
  // Hours   : 01 -> LSB=1 (Msg 6), MSB=0 (Msg 7) (assuming 24fps/Type 0)

  std::vector<uint8_t> midi1_sequence = {
      0xF1, 0x04, // Piece 0: Frame LSB = 4
      0xF1, 0x10, // Piece 1: Frame MSB = 0
      0xF1, 0x23, // Piece 2: Seconds LSB = 3
      0xF1, 0x30, // Piece 3: Seconds MSB = 0
      0xF1, 0x42, // Piece 4: Minutes LSB = 2
      0xF1, 0x50, // Piece 5: Minutes MSB = 0
      0xF1, 0x61, // Piece 6: Hours LSB = 1
      0xF1, 0x70  // Piece 7: Hours MSB = 0
  };

  std::vector<uint32_t> expected_umps
      = {0x10F10400, // System Message (1), Group 0, Status F1, Data 04
         0x10F11000, // System Message (1), Group 0, Status F1, Data 10
         0x10F12300, // ...
         0x10F13000, 0x10F14200, 0x10F15000, 0x10F16100, 0x10F17000};

  std::vector<uint32_t> result_umps;
  bool ok = false;

  auto err = m1_to_m2.convert(
      midi1_sequence.data(), midi1_sequence.size(),
      0, // Group 0
      [&](uint32_t* ump, std::size_t sz, int64_t) {
    ok = true;
    result_umps.insert(result_umps.end(), ump, ump + sz);
    return stdx::error{};
  });

  REQUIRE(err == stdx::error{});
  REQUIRE(ok);

  SECTION("Verify UMP count")
  {
    // We sent 8 MIDI 1.0 messages, we expect 8 UMP packets (32-bit each)
    REQUIRE(result_umps.size() == 8);
  }

  SECTION("Verify UMP content")
  {
    for (size_t i = 0; i < expected_umps.size(); ++i)
    {
      INFO("Checking MTC Quarter Frame index " << i);

      // Breakdown for clearer error reporting if it fails
      uint32_t res = result_umps[i];
      uint32_t exp = expected_umps[i];

      // Verify the full UMP word matches the expectation
      REQUIRE(res == exp);

      // Redundant check: explicit field validation
      // UMP Type 1 (System Common), Group 0 -> High nibble 0x1
      REQUIRE((res >> 28) == 0x1);
      // Status Byte 0xF1
      REQUIRE(((res >> 16) & 0xFF) == 0xF1);
      // Data Byte (The MTC quarter frame payload)
      REQUIRE(((res >> 8) & 0xFF) == midi1_sequence[(i * 2) + 1]);
    }
  }
}

TEST_CASE("MIDI 1 pitch bend through MIDI 2 keeps its value", "[midi_conversion][pitch_bend]")
{
  for (int pb : {0, 1, 2048, 8191, 8192, 8193, 12000, 16383})
  {
    INFO("pitch bend " << pb);
    const uint8_t bytes[3]{0xE3, uint8_t(pb & 0x7F), uint8_t(pb >> 7)};
    cmidi2_ump ump[2]{};
    REQUIRE(cmidi2_midi1_channel_voice_to_midi2(bytes, 3, ump));
    const uint32_t v32 = ump[1];
    if (pb == 0)
      CHECK(v32 == 0u);
    if (pb == 8192)
      CHECK(v32 == 0x80000000u);
    if (pb == 16383)
      CHECK(v32 == 0xFFFFFFFFu);

    libremidi::ump u;
    u.data[0] = ump[0];
    u.data[1] = ump[1];
    const auto back = libremidi::midi1_from_ump(u);
    REQUIRE(back.bytes.size() == 3);
    CHECK(back.bytes[0] == 0xE3);
    CHECK(int(back.bytes[1]) + 128 * int(back.bytes[2]) == pb);
  }
}

TEST_CASE("MIDI 1 pitch bend through the stream converter", "[midi_conversion][pitch_bend]")
{
  // midi1_to_midi2::convert goes through cmidi2_convert_midi1_to_ump, not
  // cmidi2_midi1_channel_voice_to_midi2: both have to upscale the same way.
  libremidi::midi1_to_midi2 m1;
  for (int pb : {0, 1, 2048, 8191, 8192, 8193, 12000, 16383})
  {
    INFO("pitch bend " << pb);
    const unsigned char bytes[3]{0xE3, uint8_t(pb & 0x7F), uint8_t(pb >> 7)};

    std::vector<uint32_t> stream;
    auto err = m1.convert(bytes, 3, 0, [&](uint32_t* ump, std::size_t sz, int64_t) {
      stream.assign(ump, ump + sz);
      return stdx::error{};
    });
    REQUIRE(err == stdx::error{});
    REQUIRE(stream.size() == 2);

    cmidi2_ump direct[2]{};
    REQUIRE(cmidi2_midi1_channel_voice_to_midi2(bytes, 3, direct));
    CHECK(stream[0] == direct[0]);
    CHECK(stream[1] == direct[1]);
    CHECK(stream[1] == cmidi2_upscale_14_to_32(uint32_t(pb)));
    if (pb == 8192)
      CHECK(stream[1] == 0x80000000u);
    if (pb == 16383)
      CHECK(stream[1] == 0xFFFFFFFFu);

    libremidi::ump u;
    u.data[0] = stream[0];
    u.data[1] = stream[1];
    const auto back = libremidi::midi1_from_ump(u);
    REQUIRE(back.bytes.size() == 3);
    CHECK(int(back.bytes[1]) + 128 * int(back.bytes[2]) == pb);
  }
}

namespace
{
std::vector<libremidi::ump>
sysex_packets(libremidi::midi1_to_midi2& m1, const std::vector<uint8_t>& bytes)
{
  std::vector<libremidi::ump> packets;
  auto err = m1.convert(bytes.data(), bytes.size(), 0, [&](uint32_t* ump, std::size_t sz, int64_t) {
    libremidi::for_each_ump_packet(ump, sz, 0, [&](const libremidi::ump& p) { packets.push_back(p); });
    return stdx::error{};
  });
  REQUIRE(err == stdx::error{});
  return packets;
}

std::vector<uint8_t> sysex_of(std::size_t data_bytes)
{
  std::vector<uint8_t> bytes{0xF0};
  for (std::size_t i = 0; i < data_bytes; i++)
    bytes.push_back(uint8_t(i % 0x80));
  bytes.push_back(0xF7);
  return bytes;
}

template <std::size_t N>
std::vector<std::vector<uint8_t>>
reassemble(libremidi::ump_to_midi1_stream<N>& s, const std::vector<libremidi::ump>& packets)
{
  std::vector<std::vector<uint8_t>> out;
  for (const auto& p : packets)
    if (auto m = s.feed(p); !m.empty())
      out.emplace_back(m.begin(), m.end());
  return out;
}
}

TEST_CASE("MIDI 1 sysex becomes a sequence of SysEx7 packets", "[midi_conversion][sysex]")
{
  libremidi::midi1_to_midi2 m1;

  SECTION("an MTC full frame: a start packet of 6 bytes and an end packet of 2")
  {
    const auto packets
        = sysex_packets(m1, {0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x21, 0x02, 0x03, 0x04, 0xF7});
    REQUIRE(packets.size() == 2);
    CHECK(packets[0].data[0] == 0x30167F7F);
    CHECK(packets[0].data[1] == 0x01012102);
    CHECK(packets[1].data[0] == 0x30320304);
    CHECK(packets[1].data[1] == 0);
  }

  SECTION("any length: start, continue..., end, at most 6 bytes each")
  {
    for (std::size_t n : {0, 1, 6, 7, 12, 13, 255, 256, 300, 1000, 4000})
    {
      INFO(n << " data bytes");
      const auto packets = sysex_packets(m1, sysex_of(n));
      REQUIRE(packets.size() == std::max<std::size_t>(1, (n + 5) / 6));
      std::size_t total = 0;
      for (std::size_t i = 0; i < packets.size(); i++)
      {
        const auto status = (packets[i].data[0] >> 20) & 0xF;
        const auto count = (packets[i].data[0] >> 16) & 0xF;
        CHECK(packets[i].get_type() == libremidi::midi2::message_type::SYSEX7);
        CHECK(count <= 6);
        if (packets.size() == 1)
          CHECK(status == 0);
        else if (i == 0)
          CHECK(status == 1);
        else if (i + 1 == packets.size())
          CHECK(status == 3);
        else
          CHECK(status == 2);
        total += count;
      }
      CHECK(total == n);
    }
  }

  SECTION("after another message of the same buffer")
  {
    const std::vector<uint8_t> bytes{0x90, 60, 100, 0xF0, 1, 2, 3, 4, 5, 6, 7, 8, 0xF7};
    const auto packets = sysex_packets(m1, bytes);
    REQUIRE(packets.size() == 3);
    CHECK(packets[0].get_type() == libremidi::midi2::message_type::MIDI_2_CHANNEL);
    CHECK(packets[1].data[0] == 0x30160102);
    CHECK(packets[1].data[1] == 0x03040506);
    CHECK(packets[2].data[0] == 0x30320708);
  }

  SECTION("convert_packets gives one packet per call")
  {
    std::vector<libremidi::ump> packets;
    const auto bytes = sysex_of(20);
    auto err = m1.convert_packets(
        bytes.data(), bytes.size(), 42, [&](const libremidi::ump& p) { packets.push_back(p); });
    REQUIRE(err == stdx::error{});
    REQUIRE(packets.size() == 4);
    for (auto& p : packets)
      CHECK(p.timestamp == 42);
  }
}

TEST_CASE("A single packet converts to MIDI 1 only when it holds a whole message", "[midi_conversion][sysex]")
{
  // F0 7E 01 02 F7: three data bytes in one packet
  const libremidi::ump complete{0x30037E01, 0x02000000};
  CHECK(libremidi::midi1_from_ump(complete).bytes.empty()); // five bytes do not fit in four

  uint8_t out[8]{};
  REQUIRE(cmidi2_convert_single_ump_to_midi1(out, 8, const_cast<uint32_t*>(complete.data)) == 5);
  CHECK(std::vector<uint8_t>(out, out + 5) == std::vector<uint8_t>{0xF0, 0x7E, 0x01, 0x02, 0xF7});

  const libremidi::ump start{0x30167F7F, 0x01012102};
  CHECK(cmidi2_convert_single_ump_to_midi1(out, 8, const_cast<uint32_t*>(start.data)) == 0);
}

TEST_CASE("SysEx7 packets reassemble into one MIDI 1 message", "[midi_conversion][sysex]")
{
  libremidi::midi1_to_midi2 m1;
  libremidi::ump_to_midi1_stream<> s;

  SECTION("round trip, any length")
  {
    for (std::size_t n : {0, 1, 5, 6, 7, 8, 12, 13, 255, 256, 300, 1000, 4000})
    {
      INFO(n << " data bytes");
      const auto bytes = sysex_of(n);
      const auto out = reassemble(s, sysex_packets(m1, bytes));
      REQUIRE(out.size() == 1);
      CHECK(out[0] == bytes);
      CHECK(!s.assembling());
    }
    CHECK(s.dropped() == 0);
  }

  SECTION("packets fed one call at a time, other messages in between")
  {
    const std::vector<uint8_t> frame{0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x21, 0x02, 0x03, 0x04, 0xF7};
    const auto packets = sysex_packets(m1, frame);
    REQUIRE(packets.size() == 2);

    CHECK(s.feed(packets[0]).empty());
    CHECK(s.assembling());
    const libremidi::ump clock{0x10F80000};
    const auto c = s.feed(clock);
    REQUIRE(c.size() == 1);
    CHECK(c[0] == 0xF8);
    const auto m = s.feed(packets[1]);
    CHECK(std::vector<uint8_t>(m.begin(), m.end()) == frame);
  }

  SECTION("channel and system messages pass as they come")
  {
    libremidi::ump note = libremidi::ump_from_midi1(libremidi::message{{0x93, 60, 100}, 0});
    const auto m = s.feed(note);
    CHECK(std::vector<uint8_t>(m.begin(), m.end()) == std::vector<uint8_t>{0x93, 60, 100});
  }

  SECTION("longer than the capacity: dropped whole, the next one is whole")
  {
    libremidi::ump_to_midi1_stream<64> small;
    CHECK(reassemble(small, sysex_packets(m1, sysex_of(62))).size() == 1); // 64 bytes
    CHECK(small.dropped() == 0);
    CHECK(reassemble(small, sysex_packets(m1, sysex_of(63))).empty());
    CHECK(small.dropped() == 1);
    CHECK(!small.assembling());
    const auto bytes = sysex_of(20);
    const auto out = reassemble(small, sysex_packets(m1, bytes));
    REQUIRE(out.size() == 1);
    CHECK(out[0] == bytes);
  }

  SECTION("a start cut short by another start drops the first message")
  {
    auto first = sysex_packets(m1, sysex_of(20));
    auto second = sysex_packets(m1, sysex_of(10));
    first.pop_back();
    first.insert(first.end(), second.begin(), second.end());
    const auto out = reassemble(s, first);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == sysex_of(10));
    CHECK(s.dropped() == 1);
  }

  SECTION("continue and end packets without a start give nothing")
  {
    auto packets = sysex_packets(m1, sysex_of(20));
    packets.erase(packets.begin());
    CHECK(reassemble(s, packets).empty());
    CHECK(s.dropped() == 3);
  }
}

TEST_CASE("A MIDI 1 reader of a UMP backend gets system-exclusive whole", "[midi_conversion][sysex]")
{
  std::vector<std::vector<uint8_t>> received;
  libremidi::input_configuration midi1;
  midi1.on_message = [&](libremidi::message&& m) { received.emplace_back(m.bytes.begin(), m.bytes.end()); };
  auto ump = libremidi::convert_midi1_to_midi2_input_configuration(midi1);

  libremidi::midi1_to_midi2 m1;
  const std::vector<uint8_t> frame{0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x21, 0x02, 0x03, 0x04, 0xF7};
  for (auto p : sysex_packets(m1, frame))
    ump.on_message(std::move(p));
  for (auto p : sysex_packets(m1, sysex_of(300)))
    ump.on_message(std::move(p));
  ump.on_message(libremidi::ump_from_midi1(libremidi::message{{0x90, 60, 100}, 0}));

  REQUIRE(received.size() == 3);
  CHECK(received[0] == frame);
  CHECK(received[1] == sysex_of(300));
  CHECK(received[2] == std::vector<uint8_t>{0x90, 60, 100});
}

TEST_CASE("A UMP reader of a MIDI 1 backend gets every SysEx7 packet", "[midi_conversion][sysex]")
{
  std::vector<libremidi::ump> received;
  libremidi::ump_input_configuration ump;
  ump.on_message = [&](libremidi::ump&& u) { received.push_back(u); };
  auto midi1 = libremidi::convert_midi2_to_midi1_input_configuration(ump);

  midi1.on_message(libremidi::message{{0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x21, 0x02, 0x03, 0x04, 0xF7}, 0});
  REQUIRE(received.size() == 2);
  CHECK(received[0].data[0] == 0x30167F7F);
  CHECK(received[0].data[1] == 0x01012102);
  CHECK(received[1].data[0] == 0x30320304);
  CHECK(received[1].data[2] == 0);
}
