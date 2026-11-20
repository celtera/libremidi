#include "../include_catch.hpp"

#include <libremidi/detail/conversion.hpp>
#include <libremidi/libremidi.hpp>

#include <cstdint>
#include <initializer_list>
#include <random>
#include <vector>

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

namespace
{
using bytes = std::vector<uint8_t>;
using words = std::vector<uint32_t>;
using libremidi::controller_translation;

// One converter fed message after message, as a device input callback does.
struct stream
{
  explicit stream(controller_translation c)
      : conv{c}
  {
  }

  // The UMP words this call gave; every call has to succeed.
  words feed(const bytes& message)
  {
    words out;
    const auto err = conv.convert(
        message.data(), message.size(), 0, [&](const uint32_t* ump, std::size_t n, int64_t) {
      out.insert(out.end(), ump, ump + n);
      return stdx::error{};
    });
    CHECK(err == stdx::error{});
    return out;
  }

  words feed(std::initializer_list<bytes> messages)
  {
    words out;
    for (const auto& m : messages)
    {
      const auto w = feed(m);
      out.insert(out.end(), w.begin(), w.end());
    }
    return out;
  }

  libremidi::midi1_to_midi2 conv;
};

words w64(int64_t m)
{
  return {uint32_t(uint64_t(m) >> 32), uint32_t(uint64_t(m))};
}

words operator+(words a, const words& b)
{
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

uint32_t dte(uint8_t msb, uint8_t lsb)
{
  return (uint32_t(msb) << 25) | (uint32_t(lsb) << 18);
}
words rpn(int ch, int msb, int lsb, uint32_t data)
{
  return w64(cmidi2_ump_midi2_rpn(0, ch, msb, lsb, data));
}
words nrpn(int ch, int msb, int lsb, uint32_t data)
{
  return w64(cmidi2_ump_midi2_nrpn(0, ch, msb, lsb, data));
}
words cc(int ch, int index, int value)
{
  return w64(cmidi2_ump_midi2_cc(0, ch, index, uint32_t(value) << 25));
}
words note_on(int ch, int note, int vel)
{
  return w64(cmidi2_ump_midi2_note_on(0, ch, note, 0, vel << 9, 0));
}
words program(int ch, bool bank_valid, int prog, int msb, int lsb)
{
  return w64(cmidi2_ump_midi2_program(0, ch, bank_valid ? 1 : 0, prog, msb, lsb));
}

bytes to_midi1(const words& ump)
{
  libremidi::midi2_to_midi1 back;
  bytes out;
  const auto err = back.convert(
      ump.data(), ump.size(), 0, [&](const uint8_t* midi, std::size_t n, int64_t) {
    out.insert(out.end(), midi, midi + n);
    return stdx::error{};
  });
  CHECK(err == stdx::error{});
  return out;
}

const bytes rpn_msb0{0xB0, 0x65, 0x00};
const bytes rpn_lsb0{0xB0, 0x64, 0x00};
}

TEST_CASE("the packing helpers give the words of the specification", "[controller_translation]")
{
  CHECK(rpn(0, 0, 0, dte(0x0C, 0)) == words{0x40200000, 0x18000000});
  CHECK(rpn(0, 0, 0, dte(0x0C, 0x40)) == words{0x40200000, 0x19000000});
  CHECK(rpn(0, 0, 0, dte(0x0C, 0x01)) == words{0x40200000, 0x18040000});
  CHECK(rpn(0, 0, 0, dte(0x02, 0)) == words{0x40200000, 0x04000000});
  CHECK(rpn(0, 0, 2, dte(0x40, 0)) == words{0x40200002, 0x80000000});
  CHECK(nrpn(0, 1, 2, dte(0x10, 0)) == words{0x40300102, 0x20000000});
  CHECK(nrpn(1, 1, 2, dte(0x10, 0)) == words{0x40310102, 0x20000000});
  CHECK(note_on(0, 0x3C, 0x40) == words{0x40903C00, 0x80000000});
  CHECK(cc(0, 0x60, 0) == words{0x40B06000, 0x00000000});
  CHECK(cc(0, 0x07, 0x40) == words{0x40B00700, 0x80000000});
  CHECK(cc(0, 0x27, 0x40) == words{0x40B02700, 0x80000000});
  CHECK(program(0, true, 5, 1, 2) == words{0x40C00001, 0x05000102});
  CHECK(program(0, false, 6, 0, 0) == words{0x40C00000, 0x06000000});
  CHECK(program(0, true, 7, 1, 3) == words{0x40C00001, 0x07000103});
  CHECK(program(0, true, 5, 0, 2) == words{0x40C00001, 0x05000002});
  CHECK(program(0, false, 5, 0, 0) == words{0x40C00000, 0x05000000});
}

TEST_CASE("semantic: registered and assignable controllers", "[controller_translation]")
{
  stream s{controller_translation::semantic};

  SECTION("1, 2: data entry MSB alone gives the parameter, and notes go on")
  {
    CHECK(s.feed(rpn_msb0).empty());
    CHECK(s.feed(rpn_lsb0).empty());
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == words{0x40200000, 0x18000000});
    CHECK(s.feed({0x90, 0x3C, 0x40}) == words{0x40903C00, 0x80000000});
  }

  SECTION("3, 4, 5: data entry LSB gives it again, and the selection stays")
  {
    CHECK(s.feed({rpn_msb0, rpn_lsb0}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == words{0x40200000, 0x18000000});
    CHECK(s.feed({0xB0, 0x26, 0x40}) == words{0x40200000, 0x19000000});

    SECTION("4: a new MSB starts from an LSB of 0")
    {
      CHECK(s.feed({0xB0, 0x06, 0x02}) == words{0x40200000, 0x04000000});
    }
    SECTION("5: a new LSB keeps the MSB")
    {
      CHECK(s.feed({0xB0, 0x26, 0x01}) == words{0x40200000, 0x18040000});
    }
  }

  SECTION("6: assignable controller")
  {
    CHECK(s.feed({{0xB0, 0x63, 0x01}, {0xB0, 0x62, 0x02}}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x10}) == words{0x40300102, 0x20000000});
    CHECK(s.feed({0xB0, 0x26, 0x00}) == words{0x40300102, 0x20000000});
  }

  SECTION("7: selection LSB first")
  {
    CHECK(s.feed({rpn_lsb0, rpn_msb0}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == words{0x40200000, 0x18000000});
  }

  SECTION("8: the last selection wins")
  {
    CHECK(s.feed({rpn_msb0, rpn_lsb0, {0xB0, 0x63, 0x01}, {0xB0, 0x62, 0x02}}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x10}) == words{0x40300102, 0x20000000});
  }

  SECTION("9: data entry with nothing selected is nothing, and is not kept")
  {
    CHECK(s.feed({{0xB0, 0x06, 0x05}, {0xB0, 0x26, 0x05}}).empty());
    CHECK(s.feed({rpn_msb0, rpn_lsb0}).empty());
    CHECK(s.feed({0xB0, 0x26, 0x00}) == words{0x40200000, 0x00000000});
  }

  SECTION("10: the null parameter deselects")
  {
    CHECK(
        s.feed({rpn_msb0, rpn_lsb0, {0xB0, 0x06, 0x0C}}) == words{0x40200000, 0x18000000});
    CHECK(s.feed({{0xB0, 0x65, 0x7F}, {0xB0, 0x64, 0x7F}, {0xB0, 0x06, 0x05}, {0xB0, 0x26, 0x05}})
              .empty());
    CHECK(s.feed({0x90, 0x3C, 0x40}) == note_on(0, 0x3C, 0x40));
  }

  SECTION("11: the null assignable controller")
  {
    CHECK(s.feed({{0xB0, 0x63, 0x7F}, {0xB0, 0x62, 0x7F}, {0xB0, 0x06, 0x05}}).empty());
  }

  SECTION("12: half a selection")
  {
    CHECK(s.feed({rpn_msb0, {0xB0, 0x06, 0x0C}}).empty());
    CHECK(s.feed({0x90, 0x3C, 0x40}) == note_on(0, 0x3C, 0x40));
  }

  SECTION("13: one half changes, the other stays")
  {
    CHECK(
        s.feed({rpn_msb0, rpn_lsb0, {0xB0, 0x06, 0x0C}}) == words{0x40200000, 0x18000000});
    CHECK(s.feed({0xB0, 0x64, 0x02}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x40}) == words{0x40200002, 0x80000000});
  }

  SECTION("14: a selection is of its channel")
  {
    CHECK(s.feed({rpn_msb0, rpn_lsb0, {0xB1, 0x06, 0x0C}}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == words{0x40200000, 0x18000000});
  }

  SECTION("15: two channels interleaved")
  {
    CHECK(s.feed({rpn_msb0, {0xB1, 0x63, 0x01}, rpn_lsb0, {0xB1, 0x62, 0x02}}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == words{0x40200000, 0x18000000});
    CHECK(s.feed({0xB1, 0x06, 0x10}) == words{0x40310102, 0x20000000});
  }

  SECTION("16: data increment and decrement are plain controllers")
  {
    CHECK(s.feed({rpn_msb0, rpn_lsb0}).empty());
    CHECK(s.feed({0xB0, 0x60, 0x00}) == words{0x40B06000, 0x00000000});
    CHECK(s.feed({0xB0, 0x61, 0x00}) == words{0x40B06100, 0x00000000});
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == words{0x40200000, 0x18000000});
  }

  SECTION("23: 14-bit controller pairs are two controllers")
  {
    CHECK(s.feed({0xB0, 0x07, 0x40}) == words{0x40B00700, 0x80000000});
    CHECK(s.feed({0xB0, 0x27, 0x40}) == words{0x40B02700, 0x80000000});
  }

  SECTION("24: running status in one call")
  {
    CHECK(
        s.feed({0xB0, 0x65, 0x00, 0x64, 0x00, 0x06, 0x0C, 0x26, 0x00})
        == words{0x40200000, 0x18000000, 0x40200000, 0x18000000});
  }

  SECTION("25: back to MIDI 1")
  {
    const auto first = s.feed({rpn_msb0, rpn_lsb0, {0xB0, 0x06, 0x0C}});
    const auto second = s.feed({0xB0, 0x26, 0x40});
    CHECK(
        to_midi1(first)
        == bytes{0xB0, 0x65, 0x00, 0xB0, 0x64, 0x00, 0xB0, 0x06, 0x0C, 0xB0, 0x26, 0x00});
    CHECK(
        to_midi1(second)
        == bytes{0xB0, 0x65, 0x00, 0xB0, 0x64, 0x00, 0xB0, 0x06, 0x0C, 0xB0, 0x26, 0x40});
  }

  SECTION("reset forgets the selection")
  {
    CHECK(s.feed({rpn_msb0, rpn_lsb0}).empty());
    s.conv.reset();
    CHECK(s.feed({0xB0, 0x06, 0x0C}).empty());
  }
}

TEST_CASE("semantic: bank select", "[controller_translation]")
{
  stream s{controller_translation::semantic};

  SECTION("17, 18, 19: the bank goes with the program change that follows")
  {
    CHECK(s.feed({{0xB0, 0x00, 0x01}, {0xB0, 0x20, 0x02}}).empty());
    CHECK(s.feed({0xC0, 0x05}) == words{0x40C00001, 0x05000102});

    SECTION("18: and not with the next one")
    {
      CHECK(s.feed({0xC0, 0x06}) == words{0x40C00000, 0x06000000});
    }
    SECTION("19: half a bank select completes with the half it had")
    {
      CHECK(s.feed({0xB0, 0x20, 0x03}).empty());
      CHECK(s.feed({0xC0, 0x07}) == words{0x40C00001, 0x07000103});
    }
  }

  SECTION("20: bank LSB alone")
  {
    CHECK(s.feed({0xB0, 0x20, 0x02}).empty());
    CHECK(s.feed({0xC0, 0x05}) == words{0x40C00001, 0x05000002});
  }

  SECTION("21: no bank")
  {
    CHECK(s.feed({0xC0, 0x05}) == words{0x40C00000, 0x05000000});
  }

  SECTION("22: a bank is of its channel")
  {
    CHECK(s.feed({{0xB0, 0x00, 0x01}, {0xB0, 0x20, 0x02}}).empty());
    CHECK(s.feed({0xC1, 0x05}) == words{0x40C10000, 0x05000000});
    CHECK(s.feed({0xC0, 0x05}) == words{0x40C00001, 0x05000102});
  }
}

TEST_CASE("raw: each message gives its own UMP", "[controller_translation]")
{
  stream s{controller_translation::raw};

  SECTION("default")
  {
    CHECK(libremidi::midi1_to_midi2{}.controllers == controller_translation::raw);
  }

  SECTION("the pitch bend range sequence with no data entry LSB")
  {
    CHECK(s.feed(rpn_msb0) == cc(0, 0x65, 0));
    CHECK(s.feed(rpn_lsb0) == cc(0, 0x64, 0));
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == cc(0, 0x06, 0x0C));
    CHECK(s.feed({0x90, 0x3C, 0x40}) == words{0x40903C00, 0x80000000});
  }

  SECTION("every controller the semantic translation swallows")
  {
    for (int index : {0, 6, 32, 38, 96, 97, 98, 99, 100, 101})
    {
      INFO("controller " << index);
      const bytes in{0xB3, uint8_t(index), 0x55};
      const auto out = s.feed(in);
      CHECK(out == cc(3, index, 0x55));
      CHECK(to_midi1(out) == in);
    }
  }

  SECTION("a program change carries no bank")
  {
    CHECK(s.feed({0xB0, 0x00, 0x01}) == cc(0, 0, 1));
    CHECK(s.feed({0xB0, 0x20, 0x02}) == cc(0, 32, 2));
    CHECK(s.feed({0xC0, 0x05}) == words{0x40C00000, 0x05000000});
  }

  SECTION("running status in one call")
  {
    const bytes in{0xB0, 0x65, 0x00, 0x64, 0x00, 0x06, 0x0C, 0x26, 0x00};
    const auto out = s.feed(in);
    CHECK(out == cc(0, 0x65, 0) + cc(0, 0x64, 0) + cc(0, 0x06, 0x0C) + cc(0, 0x26, 0));
    CHECK(
        to_midi1(out)
        == bytes{0xB0, 0x65, 0x00, 0xB0, 0x64, 0x00, 0xB0, 0x06, 0x0C, 0xB0, 0x26, 0x00});
  }

  SECTION("the mode can change between two messages")
  {
    CHECK(s.feed(rpn_msb0) == cc(0, 0x65, 0));
    s.conv.controllers = controller_translation::semantic;
    CHECK(s.feed({rpn_msb0, rpn_lsb0}).empty());
    CHECK(s.feed({0xB0, 0x06, 0x0C}) == words{0x40200000, 0x18000000});
  }
}

TEST_CASE("controller translation: system messages in both modes", "[controller_translation]")
{
  for (auto mode : {controller_translation::raw, controller_translation::semantic})
  {
    stream s{mode};
    // In the middle of a parameter selection, which must not matter.
    s.feed({rpn_msb0, rpn_lsb0});

    for (const bytes& in :
         {bytes{0xF8}, bytes{0xFA}, bytes{0xF2, 0x01, 0x02}, bytes{0xF3, 0x05}, bytes{0xF6},
          bytes{0xF0, 0x7E, 0x00, 0x01, 0x02, 0xF7},
          bytes{0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xF7}})
    {
      const auto out = s.feed(in);
      REQUIRE(!out.empty());
      CHECK(to_midi1(out) == in);
    }
  }
}

TEST_CASE("controller translation: what was translated is given", "[controller_translation]")
{
  for (auto mode : {controller_translation::raw, controller_translation::semantic})
  {
    libremidi::midi1_to_midi2 conv{mode};
    const auto convert = [&](const bytes& in, words& out) {
      return conv.convert(
          in.data(), in.size(), 0, [&](const uint32_t* ump, std::size_t n, int64_t) {
        out.insert(out.end(), ump, ump + n);
        return stdx::error{};
      });
    };

    SECTION(mode == controller_translation::raw ? "raw: before an error" : "semantic: before an error")
    {
      // A note, then a truncated one.
      words out;
      CHECK(convert({0x90, 0x3C, 0x40, 0x90, 0x3E}, out) != stdx::error{});
      CHECK(out == note_on(0, 0x3C, 0x40));

      // A system-exclusive message with no end.
      out.clear();
      CHECK(convert({0x90, 0x3C, 0x40, 0xF0, 0x01, 0x02}, out) != stdx::error{});
      CHECK(out == note_on(0, 0x3C, 0x40));

      // A data byte with no status.
      out.clear();
      CHECK(convert({0x3C, 0x40}, out) != stdx::error{});
      CHECK(out.empty());

      // And the converter goes on.
      out.clear();
      CHECK(convert({0x90, 0x3C, 0x40}, out) == stdx::error{});
      CHECK(out == note_on(0, 0x3C, 0x40));
    }

    SECTION(mode == controller_translation::raw ? "raw: more than the buffer" : "semantic: more than the buffer")
    {
      // Two bytes in, two words out: more words than the converter holds.
      const std::size_t count = libremidi::midi1_to_midi2::capacity;
      bytes in;
      for (std::size_t i = 0; i < count; i++)
      {
        in.push_back(0xC0);
        in.push_back(uint8_t(i % 128));
      }
      words out;
      CHECK(convert(in, out) == stdx::error{});
      REQUIRE(out.size() == count * 2);
      for (std::size_t i = 0; i < count; i++)
        REQUIRE(words{out[2 * i], out[2 * i + 1]} == program(0, false, int(i % 128), 0, 0));
    }
  }
}

TEST_CASE("26: random controller fragments and notes", "[controller_translation]")
{
  for (auto mode : {controller_translation::raw, controller_translation::semantic})
  {
    INFO((mode == controller_translation::raw ? "raw" : "semantic"));
    std::mt19937 rng{0x5eed};
    const uint8_t special[]{0, 6, 32, 38, 96, 97, 98, 99, 100, 101};
    const uint8_t values[]{0, 1, 0x0C, 0x40, 0x7F, 0x7F, 0x7F};

    libremidi::midi1_to_midi2 conv{mode};
    libremidi::midi2_to_midi1 back;
    bytes sent;
    bytes sent_notes;
    bytes received;
    int errors = 0;
    int swallowed = 0;

    for (int i = 0; i < 10000; i++)
    {
      const uint8_t ch = rng() % 16;
      bytes in;
      switch (rng() % 8)
      {
        case 0:
        case 1:
          in = {uint8_t(0x90 | ch), uint8_t(rng() % 128), uint8_t(1 + rng() % 127)};
          break;
        case 2:
          in = {uint8_t(0x80 | ch), uint8_t(rng() % 128), uint8_t(rng() % 128)};
          break;
        case 3:
          in = {uint8_t(0xC0 | ch), uint8_t(rng() % 128)};
          break;
        default:
          in = {uint8_t(0xB0 | ch), special[rng() % std::size(special)],
                values[rng() % std::size(values)]};
          break;
      }
      sent.insert(sent.end(), in.begin(), in.end());
      if ((in[0] & 0xE0) == 0x80)
        sent_notes.insert(sent_notes.end(), in.begin(), in.end());

      words out;
      const auto err = conv.convert(
          in.data(), in.size(), 0, [&](const uint32_t* ump, std::size_t n, int64_t) {
        out.insert(out.end(), ump, ump + n);
        return stdx::error{};
      });
      if (err != stdx::error{})
        errors++;
      if (out.empty())
        swallowed++;
      else
        REQUIRE(out.size() == 2);

      if (mode == controller_translation::semantic && !out.empty()
          && ((out[0] >> 20) & 0xF) == 0xB)
      {
        // No controller that is part of a longer message comes out as one.
        const int index = (out[0] >> 8) & 0x7F;
        const bool part = index == 0 || index == 6 || index == 32 || index == 38
                          || (index >= 98 && index <= 101);
        REQUIRE(!part);
      }

      if (!out.empty())
      {
        const auto err2 = back.convert(
            out.data(), out.size(), 0, [&](const uint8_t* midi, std::size_t n, int64_t) {
          received.insert(received.end(), midi, midi + n);
          return stdx::error{};
        });
        if (err2 != stdx::error{})
          errors++;
      }
    }

    CHECK(errors == 0);
    if (mode == controller_translation::raw)
    {
      CHECK(swallowed == 0);
      CHECK(received == sent);
    }
    else
    {
      CHECK(swallowed > 0);
      // Every note arrives, in order.
      bytes received_notes;
      for (std::size_t i = 0; i < received.size();)
      {
        const std::size_t n = (received[i] & 0xE0) == 0xC0 ? 2 : 3;
        if ((received[i] & 0xE0) == 0x80)
          received_notes.insert(
              received_notes.end(), received.begin() + i, received.begin() + i + n);
        i += n;
      }
      CHECK(received_notes == sent_notes);
    }
  }
}

TEST_CASE("UMP to MIDI 1: controllers and parameters", "[controller_translation]")
{
  SECTION("a MIDI 2 control change is the MIDI 1 one of the same index")
  {
    for (int index : {0, 6, 32, 38, 98, 99, 100, 101})
      CHECK(to_midi1(cc(2, index, 0x33)) == bytes{0xB2, uint8_t(index), 0x33});
  }

  SECTION("a registered controller is four control changes")
  {
    CHECK(
        to_midi1(rpn(1, 0, 2, dte(0x40, 0x01)))
        == bytes{0xB1, 0x65, 0x00, 0xB1, 0x64, 0x02, 0xB1, 0x06, 0x40, 0xB1, 0x26, 0x01});
  }

  SECTION("an assignable controller is four control changes")
  {
    CHECK(
        to_midi1(nrpn(1, 3, 4, dte(0x40, 0x01)))
        == bytes{0xB1, 0x63, 0x03, 0xB1, 0x62, 0x04, 0xB1, 0x06, 0x40, 0xB1, 0x26, 0x01});
  }

  SECTION("a program change with a bank is bank select then program change")
  {
    CHECK(
        to_midi1(program(0, true, 5, 1, 2)) == bytes{0xB0, 0x00, 0x01, 0xB0, 0x20, 0x02, 0xC0, 0x05});
    CHECK(to_midi1(program(0, false, 5, 0, 0)) == bytes{0xC0, 0x05});
  }

  SECTION("a packet with no MIDI 1 form gives nothing and no error")
  {
    // Per-note pitch bend.
    libremidi::midi2_to_midi1 back;
    const words in{0x40603C00, 0x80000000};
    int calls = 0;
    const auto err
        = back.convert(in.data(), in.size(), 0, [&](const uint8_t*, std::size_t, int64_t) {
      calls++;
      return stdx::error{};
    });
    CHECK(err == stdx::error{});
    CHECK(calls == 0);
  }

  SECTION("what was translated before an error is given")
  {
    // A note, then the start of a system-exclusive message that does not end.
    libremidi::midi2_to_midi1 back;
    words in = note_on(0, 0x3C, 0x40);
    in.push_back(0x30160102);
    in.push_back(0x03040506);
    bytes out;
    const auto err
        = back.convert(in.data(), in.size(), 0, [&](const uint8_t* midi, std::size_t n, int64_t) {
      out.insert(out.end(), midi, midi + n);
      return stdx::error{};
    });
    CHECK(err != stdx::error{});
    CHECK(out == bytes{0x90, 0x3C, 0x40});
  }
}

// The configuration converter is an implementation detail of midi_in.cpp,
// visible from outside only when it is compiled into the test.
#if defined(LIBREMIDI_HEADER_ONLY)
TEST_CASE("UMP input over a MIDI 1 backend takes the mode", "[controller_translation]")
{
  for (auto mode : {controller_translation::raw, controller_translation::semantic})
  {
    words got;
    libremidi::ump_input_configuration ump;
    ump.midi1_controllers = mode;
    ump.on_message = [&](libremidi::ump&& u) {
      got.push_back(u.data[0]);
      got.push_back(u.data[1]);
    };
    auto midi1 = libremidi::convert_midi2_to_midi1_input_configuration(ump);
    for (const bytes& m : {rpn_msb0, rpn_lsb0, bytes{0xB0, 0x06, 0x0C}, bytes{0x90, 0x3C, 0x40}})
      midi1.on_message(libremidi::message{{m.begin(), m.end()}, 0});

    if (mode == controller_translation::raw)
      CHECK(got == cc(0, 0x65, 0) + cc(0, 0x64, 0) + cc(0, 0x06, 0x0C) + note_on(0, 0x3C, 0x40));
    else
      CHECK(got == rpn(0, 0, 0, dte(0x0C, 0)) + note_on(0, 0x3C, 0x40));
  }
}
#endif
