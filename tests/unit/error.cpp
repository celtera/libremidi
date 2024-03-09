#include "../include_catch.hpp"
#include <libremidi/error.hpp>
#include <libremidi/libremidi.hpp>
#include <libremidi/backends.hpp>


TEST_CASE("error code retrieval", "[error]")
{
  stdx::error e;
#if defined(__APPLE__)
  e = libremidi::from_osstatus(kMIDIInvalidPort);
  REQUIRE(e.message() == "Invalid Port");
  REQUIRE(e.domain().name() == "coremidi");
#endif
}
