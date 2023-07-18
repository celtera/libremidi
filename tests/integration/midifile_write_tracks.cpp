#include "../include_catch.hpp"

#include <libremidi/reader.hpp>
#include <libremidi/writer.hpp>

#include <filesystem>
TEST_CASE("write multiple tracks to file", "[midi_writer]")
{
  const uint8_t key1 = 60;
  const uint8_t key2 = 42;

  const std::string filename = "multitrack_test.mid";

  {
    libremidi::writer writer;

    const auto message1 = libremidi::message::note_on(1, key1, 127);
    const auto message2 = libremidi::message::note_on(1, key2, 127);

    writer.add_event(0, 0, message1);
    writer.add_event(0, 1, message2);

    std::ofstream file{filename, std::ios::binary | std::ios::trunc};
    writer.write(file);
  }

  {
    std::ifstream file{filename, std::ios::binary};
    std::vector<uint8_t> bytes;
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    libremidi::reader reader;
    libremidi::reader::parse_result result{};
    REQUIRE_NOTHROW(result = reader.parse(bytes));
    CHECK(result == libremidi::reader::validated);

    libremidi::message message1;
    libremidi::message message2;

    REQUIRE_NOTHROW(message1 = reader.tracks.at(0).at(0).m);
    REQUIRE_NOTHROW(message2 = reader.tracks.at(1).at(0).m);

    CHECK(message1.bytes[1] == 60);
    CHECK(message2.bytes[1] == 42);
  }
}
