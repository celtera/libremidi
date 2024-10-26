#include "../include_catch.hpp"

#include <libremidi/detail/conversion.hpp>
#include <libremidi/libremidi.hpp>

#include <array>
#include <bit>

TEST_CASE("convert midi1 to midi2", "[midi_in]")
{
  libremidi::midi1_to_midi2 m1;
  GIVEN("Correct input")
  {
    bool ok = false;
    auto msg = libremidi::channel_events::control_change(3, 35, 100);

    std::vector<uint32_t> res;
    auto err
        = m1.convert(msg.bytes.data(), msg.size(), 0, [&](uint32_t* ump, std::size_t sz, int64_t) {
      ok = true;
      res.assign(ump, ump + sz);
      return stdx::error{};
    });

    THEN("Correct conversion")
    {
      REQUIRE(err == stdx::error{});
      REQUIRE(ok);
      auto expected = cmidi2_ump_midi2_cc(0, 2, 35, 100 << 25);
      union
      {
        uint32_t u[2];
        int64_t i;
      } e{.i = expected};

      REQUIRE(res.size() == 2);
      REQUIRE(e.u[1] == res[0]);
      REQUIRE(e.u[0] == res[1]);
    }
  }

  GIVEN("Wrong input")
  {
    bool ok = false;
    unsigned char arr[1]{156};
    auto err = m1.convert(arr, 1, 0, [&](uint32_t*, std::size_t, int64_t) {
      ok = true;
      return stdx::error{};
    });
    THEN("We get an error")
    {
      REQUIRE(err != stdx::error{});
      REQUIRE(ok == false);
    }
  }
}

TEST_CASE("convert midi2 to midi1", "[midi_in]")
{
  libremidi::midi2_to_midi1 m1;
  GIVEN("Correct input")
  {
    bool ok = false;
    auto msg = cmidi2_ump_midi2_cc(0, 2, 35, 100 << 25);
    union
    {
      uint32_t u[2];
      int64_t i;
    } e{.i = msg};
    using namespace std;
    swap(e.u[0], e.u[1]);

    std::vector<uint8_t> res;
    auto err = m1.convert(e.u, 2, 0, [&](uint8_t* midi, std::size_t sz, int64_t) {
      ok = true;
      res.assign(midi, midi + sz);
      return stdx::error{};
    });

    THEN("Correct conversion")
    {
      REQUIRE(err == stdx::error{});
      REQUIRE(ok);
      auto msg = libremidi::channel_events::control_change(3, 35, 100);

      REQUIRE(res.size() == 3);
      REQUIRE(res == std::vector<uint8_t>(msg.begin(), msg.end()));
    }
  }

  GIVEN("Wrong input")
  {
    bool ok = false;
    uint32_t res = 0xFFFFFFFF;
    auto err = m1.convert(&res, 1, 0, [&](uint8_t*, std::size_t, int64_t) {
      ok = true;
      return stdx::error{};
    });
    THEN("We get an error")
    {
      REQUIRE(err != stdx::error{});
      REQUIRE(ok == false);
    }
  }
}
