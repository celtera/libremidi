#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <rtmidi17/rtmidi17.hpp>


TEST_CASE("sending messages with span", "[midi_out]" ) {
  rtmidi::midi_out midi{rtmidi::API::DUMMY, "dummy"};
  midi.open_port();

  unsigned char data[3]{};
  midi.send_message(std::span<unsigned char>(data, 3));
}

TEST_CASE("sending chunked messages", "[midi_out]" ) {
  rtmidi::midi_out midi{rtmidi::API::DUMMY, "dummy"};
  midi.open_port();

  int chunk_count = 0;
  midi.set_chunking_parameters(rtmidi::chunking_parameters{
    .interval = std::chrono::milliseconds(100),
    .size = 4096, // 4kb
    .wait = [&] (const rtmidi::chunking_parameters&) { chunk_count++; }
  });

  unsigned char data[16384]{};
  midi.send_message(std::span<unsigned char>(data, 16384));

  // REQUIRE(chunk_count == 4);
}