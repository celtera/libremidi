#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <remidi/remidi.hpp>


TEST_CASE( "sending messages with span", "[midi_out]" ) {
  remidi::midi_out midi{remidi::API::DUMMY, "dummy"};
  midi.open_port();

  unsigned char data[3]{};
  midi.send_message(std::span<unsigned char>(data, 3));
}