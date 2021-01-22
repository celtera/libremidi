#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <rtmidi17/reader.hpp>

#include <filesystem>

TEST_CASE("read files from corpus", "[midi_reader]" ) {

  using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;
  std::vector<uint8_t> bytes;
  for (const auto& dirEntry : recursive_directory_iterator(RTMIDI_TEST_CORPUS))
  {
    std::cout << dirEntry << std::endl;
    if(dirEntry.is_regular_file())
    {
      if(dirEntry.path().extension() == ".mid")
      {
        bytes.clear();

        std::ifstream file{dirEntry.path(), std::ios::binary};

        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        rtmidi::reader r;
        REQUIRE_NOTHROW(r.parse(bytes));


      }
    }
  }
}
