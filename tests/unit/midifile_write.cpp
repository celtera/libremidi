#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <libremidi/reader.hpp>
#include <libremidi/writer.hpp>
#include <filesystem>

TEST_CASE("write an empty file", "[midi_writer]" )
{
  libremidi::writer writer;
  std::ofstream empty;
  writer.write(empty);
}

TEST_CASE("write an empty track", "[midi_writer]" )
{
  libremidi::writer writer;
  writer.tracks.resize(1);
  std::ofstream empty;
  writer.write(empty);
}

TEST_CASE("write multiple empty track", "[midi_writer]" )
{
  libremidi::writer writer;
  writer.tracks.resize(2);
  std::ofstream empty;
  writer.write(empty);
}

TEST_CASE("write a track with an empty event", "[midi_writer]" )
{
  libremidi::writer writer;
  writer.tracks.push_back(libremidi::midi_track{libremidi::track_event{}});
  std::ofstream empty;
  writer.write(empty);
}

TEST_CASE("write a track with a note", "[midi_writer]" )
{
  libremidi::writer writer;
  writer.tracks.push_back(
    libremidi::midi_track{
      libremidi::track_event{0, 0, libremidi::message::note_on(1, 45, 35)}});
  std::ofstream empty;
  writer.write(empty);
}
