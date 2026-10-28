#include "../include_catch.hpp"

#include <libremidi/configurations.hpp>
#include <libremidi/detail/midi_out.hpp>
#include <libremidi/libremidi.hpp>

#include <set>

TEST_CASE("creation", "[midi_out]")
{
#if defined(LIBREMIDI_CI)
  SKIP("GH runners do not have MIDI support");
#endif

  GIVEN("A default midi output")
  {
    libremidi::midi_out out;
    THEN("created with the default MIDI 1 api for the platform")
    {
      REQUIRE(out.get_current_api() == libremidi::midi1::default_api());
    }
  }

  GIVEN("A midi output with an explicitly unspecified API")
  {
    libremidi::midi_out out({}, libremidi::API::UNSPECIFIED);
    THEN("created with the default api")
    {
      REQUIRE(out.get_current_api() == libremidi::midi1::default_api());
    }
  }

  GIVEN("A midi output with an empty API")
  {
    libremidi::midi_out out({}, libremidi::output_api_configuration{});
    THEN("created with defaultapi")
    {
      REQUIRE(out.get_current_api() == libremidi::midi1::default_api());
    }
  }
  GIVEN("A midi output with an explicit API")
  {
    libremidi::midi_out out({}, libremidi::API::JACK_MIDI);
    THEN("created with that api")
    {
      REQUIRE(out.get_current_api() == libremidi::API::JACK_MIDI);
    }
  }
}

TEST_CASE("sending messages with span", "[midi_out]")
{
  libremidi::midi_out midi{{}, libremidi::dummy_configuration{}};
  midi.open_virtual_port();

  unsigned char data[3]{};
  midi.send_message(std::span<unsigned char>(data, 3));
}

#if defined(LIBREMIDI_ALSA)
  #include <libremidi/backends/alsa_raw/config.hpp>
TEST_CASE("sending chunked messages", "[midi_out]")
{
  #if defined(LIBREMIDI_CI)
  SKIP("GH runners do not have MIDI support");
  #endif

  std::set<int> written_bytes;

  libremidi::midi_out midi{
      libremidi::output_configuration{},
      libremidi::alsa_raw_output_configuration{
          .chunking = libremidi::chunking_parameters{
              .interval = std::chrono::milliseconds(100),
              .size = 4096, // 4kb
              .wait = [&](const std::chrono::microseconds&, int sz) {
    written_bytes.insert(sz);
    return true;
  }}}};

  if (auto ports
      = libremidi::observer{{}, libremidi::alsa_raw_observer_configuration{}}.get_output_ports();
      ports.size() > 0)
  {
    midi.open_port(ports.front());

    unsigned char data[16384]{};
    midi.send_message(std::span<unsigned char>(data, 16384));

    REQUIRE(written_bytes == std::set<int>{4096, 8192, 12288});
  }
  else
  {
    WARN("No MIDI output found, skipping test");
  }
}
#endif

namespace
{
// A MIDI 1 backend that keeps every message it is asked to send.
class recording_midi1_out final : public libremidi::midi1::out_api
{
public:
  libremidi::API get_current_api() const noexcept override { return libremidi::API::DUMMY; }
  stdx::error open_port(const libremidi::output_port&, std::string_view) override { return {}; }
  stdx::error close_port() override { return {}; }
  stdx::error send_message(const unsigned char* message, std::size_t size) override
  {
    sent.emplace_back(message, message + size);
    return {};
  }

  std::vector<std::vector<unsigned char>> sent;
};

std::vector<libremidi::ump> sysex7_packets(const std::vector<unsigned char>& sysex)
{
  libremidi::midi1_to_midi2 conv;
  std::vector<libremidi::ump> packets;
  const auto err = conv.convert_packets(
      sysex.data(), sysex.size(), 0, [&](const libremidi::ump& p) { packets.push_back(p); });
  REQUIRE(err == stdx::error{});
  return packets;
}

std::vector<unsigned char> sysex_of(std::size_t data_bytes)
{
  std::vector<unsigned char> bytes{0xF0};
  for (std::size_t i = 0; i < data_bytes; i++)
    bytes.push_back((unsigned char)(i % 0x80));
  bytes.push_back(0xF7);
  return bytes;
}
}

TEST_CASE("a MIDI 1 output sends a system-exclusive message given as SysEx7 packets", "[midi_out]")
{
  // MIDI Tuning Standard, real-time single note tuning change of four keys:
  // 24 bytes, four SysEx7 packets.
  const std::vector<unsigned char> mts{0xF0, 0x7F, 0x7F, 0x08, 0x02, 0x00, 0x04, 0x3C, 0x3C,
                                       0x00, 0x00, 0x3D, 0x3C, 0x40, 0x00, 0x3E, 0x3E, 0x00,
                                       0x00, 0x3F, 0x3E, 0x40, 0x00, 0xF7};
  const auto packets = sysex7_packets(mts);
  REQUIRE(packets.size() > 1);

  recording_midi1_out out;

  SECTION("one packet per call, as a graph port gives them")
  {
    for (const auto& p : packets)
      REQUIRE(out.send_ump(p.data, p.size()) == stdx::error{});
    REQUIRE(out.sent.size() == 1);
    CHECK(out.sent[0] == mts);
  }

  SECTION("all packets in one call")
  {
    std::vector<uint32_t> words;
    for (const auto& p : packets)
      words.insert(words.end(), p.begin(), p.end());
    REQUIRE(out.send_ump(words.data(), words.size()) == stdx::error{});
    REQUIRE(out.sent.size() == 1);
    CHECK(out.sent[0] == mts);
  }

  SECTION("channel messages in between keep their place")
  {
    const auto note = libremidi::ump_from_midi1(libremidi::message{{0x90, 60, 100}, 0});
    REQUIRE(out.send_ump(packets[0].data, packets[0].size()) == stdx::error{});
    REQUIRE(out.send_ump(note.data, note.size()) == stdx::error{});
    for (std::size_t i = 1; i < packets.size(); i++)
      REQUIRE(out.send_ump(packets[i].data, packets[i].size()) == stdx::error{});
    REQUIRE(out.sent.size() == 2);
    CHECK(out.sent[0] == std::vector<unsigned char>{0x90, 60, 100});
    CHECK(out.sent[1] == mts);
  }

  SECTION("as long as the output holds")
  {
    const auto bytes = sysex_of(4094);
    for (const auto& p : sysex7_packets(bytes))
      REQUIRE(out.send_ump(p.data, p.size()) == stdx::error{});
    REQUIRE(out.sent.size() == 1);
    CHECK(out.sent[0] == bytes);
  }

  SECTION("longer than the output holds: dropped, said so, the next one goes out")
  {
    const auto too_long = sysex7_packets(sysex_of(4095));
    stdx::error last{};
    for (const auto& p : too_long)
      last = out.send_ump(p.data, p.size());
    CHECK(last == std::errc::message_size);
    CHECK(out.sent.empty());

    for (const auto& p : packets)
      REQUIRE(out.send_ump(p.data, p.size()) == stdx::error{});
    REQUIRE(out.sent.size() == 1);
    CHECK(out.sent[0] == mts);
  }
}
