#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <rtmidi17/rtmidi17.hpp>


TEST_CASE("sending messages with span", "[midi_out]" ) {
  rtmidi::midi_out midi{rtmidi::API::DUMMY, "dummy"};
  midi.open_port();

  unsigned char data[3]{};
  midi.send_message(std::span<unsigned char>(data, 3));
}

#if defined(__linux__)
TEST_CASE("sending chunked messages", "[midi_out]" ) {
  rtmidi::midi_out midi{rtmidi::API::LINUX_ALSA_RAW, "dummy"};
  midi.open_port();

  std::set<int> written_bytes;
  midi.set_chunking_parameters(rtmidi::chunking_parameters{
    .interval = std::chrono::milliseconds(100),
    .size = 4096, // 4kb
    .wait = [&] (const std::chrono::microseconds&, int sz) {
       written_bytes.insert(sz);
       return true;
    }
  });

  unsigned char data[16384]{};
  midi.send_message(std::span<unsigned char>(data, 16384));

  REQUIRE(written_bytes == std::set<int>{4096, 8192, 12288});
}
#endif
