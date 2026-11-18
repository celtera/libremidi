#include "../include_catch.hpp"

#include <libremidi/backends.hpp>
#include <libremidi/error.hpp>
#include <libremidi/libremidi.hpp>

TEST_CASE("error code retrieval", "[error]")
{
  stdx::error e;
#if defined(__APPLE__)
  e = libremidi::from_osstatus(kMIDIInvalidPort);
  REQUIRE(e.message() == "Invalid Port");
  REQUIRE(e.domain().name() == "coremidi");
#endif
}

TEST_CASE("copied error retains its system error code", "[error]")
{
  const std::error_code code{42, std::system_category()};
  stdx::error copy;
  {
    stdx::error original{code};
    copy = original;
  }

  stdx::error moved{std::move(copy)};
  try
  {
    moved.throw_exception();
  }
  catch (const std::system_error& e)
  {
    REQUIRE(e.code() == code);
  }
}
