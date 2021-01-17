#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <rtmidi17/rtmidi17.hpp>


TEST_CASE( "sending messages with span", "[midi_out]" ) {
  rtmidi::midi_out midi{rtmidi::API::DUMMY, "dummy"};
  midi.open_port();

  unsigned char data[3]{};
  midi.send_message(std::span<unsigned char>(data, 3));
}