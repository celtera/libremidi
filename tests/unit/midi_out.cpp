#include "../include_catch.hpp"

#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

#include <set>

TEST_CASE("sending messages with span", "[midi_out]")
{
  libremidi::midi_out midi{{}, libremidi::dummy_configuration{}};
  midi.open_virtual_port();

  unsigned char data[3]{};
  midi.send_message(std::span<unsigned char>(data, 3));
}

#if !defined(LIBREMIDI_CI)
  #if defined(__linux__)
    #include <libremidi/backends/alsa_raw/config.hpp>
TEST_CASE("sending chunked messages", "[midi_out]")
{
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
#endif
