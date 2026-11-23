// Which backend is selected, without opening any port: these rules must hold
// whether or not the preferred backend of the platform is compiled in or available.
#include <libremidi/backends.hpp>
#include <libremidi/libremidi-c.h>
#include <libremidi/libremidi.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace
{
// A backend of another platform: never compiled in here.
#if defined(_WIN32)
constexpr auto foreign_api = libremidi::API::COREMIDI;
constexpr auto foreign_c_api = COREMIDI;
#else
constexpr auto foreign_api = libremidi::API::WINDOWS_MM;
constexpr auto foreign_c_api = WINDOWS_MM;
#endif

bool compiled(libremidi::API api)
{
  bool ret = false;
  libremidi::midi_any::for_compiled_backend(api, [&](auto&) { ret = true; });
  return ret;
}

struct counted_errors
{
  int count = 0;
  auto callback()
  {
    return [this](std::string_view, const libremidi::source_location&) { ++count; };
  }
};

// A default configuration names the preferred backend exactly when it can be
// used, and otherwise leaves the choice to the constructor.
template <typename Configuration>
void check_default(const Configuration& conf, libremidi::API preferred)
{
  if (libremidi::midi_any::is_available(preferred))
    CHECK(libremidi::midi_api(conf) == preferred);
  else
    CHECK(libremidi::get_if<libremidi::unspecified_configuration>(&conf));
}

struct plain
{
  static bool available() noexcept { return true; }
};
struct with_service
{
  static inline int started = 0;
  static bool available() noexcept { return true; }
  static bool ready() noexcept { return ++started, false; }
};
struct throws
{
  static bool available() noexcept { return true; }
};
}

TEST_CASE("the foreign backend of this test is not compiled", "[selection]")
{
  REQUIRE_FALSE(compiled(foreign_api));
  REQUIRE_FALSE(libremidi::midi_any::is_available(foreign_api));
}

TEST_CASE("default configurations fall back to automatic selection", "[selection]")
{
  check_default(libremidi::midi1::in_default_configuration(), libremidi::midi1::default_api());
  check_default(libremidi::midi1::out_default_configuration(), libremidi::midi1::default_api());
  check_default(
      libremidi::midi1::observer_default_configuration(), libremidi::midi1::default_api());
  check_default(libremidi::midi2::in_default_configuration(), libremidi::midi2::default_api());
  check_default(libremidi::midi2::out_default_configuration(), libremidi::midi2::default_api());
  check_default(
      libremidi::midi2::observer_default_configuration(), libremidi::midi2::default_api());
}

TEST_CASE("a preferred backend that cannot be used is not an error", "[selection]")
{
  // Otherwise the objects below would start the service of that backend.
  if (libremidi::midi_any::is_available(libremidi::midi2::default_api()))
    SKIP("the preferred MIDI 2 backend is available here");

  counted_errors errors;

  libremidi::ump_input_configuration in;
  in.on_message = [](libremidi::ump&&) {};
  in.on_error = errors.callback();
  CHECK(
      libremidi::midi_in{in, libremidi::midi2::in_default_configuration()}.get_current_api()
      != libremidi::API::DUMMY);

  libremidi::output_configuration out;
  out.on_error = errors.callback();
  CHECK(
      libremidi::midi_out{out, libremidi::midi2::out_default_configuration()}.get_current_api()
      != libremidi::API::DUMMY);

  libremidi::observer_configuration obs;
  obs.on_error = errors.callback();
  CHECK(
      libremidi::observer{obs, libremidi::midi2::observer_default_configuration()}
          .get_current_api()
      != libremidi::API::DUMMY);

  CHECK(errors.count == 0);
}

TEST_CASE("an explicitly requested backend is never substituted", "[selection]")
{
  counted_errors errors;

  libremidi::input_configuration in;
  in.on_message = [](libremidi::message&&) {};
  in.on_error = errors.callback();
  libremidi::output_configuration out;
  out.on_error = errors.callback();
  libremidi::observer_configuration obs;
  obs.on_error = errors.callback();

  SECTION("as an API value")
  {
    CHECK(libremidi::midi_in{in, foreign_api}.get_current_api() == libremidi::API::DUMMY);
    CHECK(libremidi::midi_out{out, foreign_api}.get_current_api() == libremidi::API::DUMMY);
    CHECK(libremidi::observer{obs, foreign_api}.get_current_api() == libremidi::API::DUMMY);
    CHECK(errors.count == 3);
  }

  SECTION("through the configuration made for that API")
  {
    const auto in_api = libremidi::midi_in_configuration_for(foreign_api);
    const auto out_api = libremidi::midi_out_configuration_for(foreign_api);
    const auto obs_api = libremidi::observer_configuration_for(foreign_api);
    REQUIRE(libremidi::get_if<libremidi::API>(&in_api));
    CHECK(*libremidi::get_if<libremidi::API>(&in_api) == foreign_api);

    CHECK(libremidi::midi_in{in, in_api}.get_current_api() == libremidi::API::DUMMY);
    CHECK(libremidi::midi_out{out, out_api}.get_current_api() == libremidi::API::DUMMY);
    CHECK(libremidi::observer{obs, obs_api}.get_current_api() == libremidi::API::DUMMY);
    CHECK(errors.count == 3);
  }

  SECTION("the dummy backend can be requested")
  {
    CHECK(
        libremidi::midi_out{out, libremidi::API::DUMMY}.get_current_api()
        == libremidi::API::DUMMY);
    CHECK(errors.count == 0);
  }
}

TEST_CASE("listing backends does not make them ready", "[selection]")
{
  // Test cases run in any order, and the next one also asks this backend.
  const int before = with_service::started;
  CHECK(libremidi::midi_any::ready<plain>());
  CHECK(libremidi::midi_any::is_available(libremidi::API::DUMMY));
  CHECK(with_service::started == before);
  CHECK_FALSE(libremidi::midi_any::ready<with_service>());
  CHECK(with_service::started == before + 1);
}

TEST_CASE("automatic selection skips a backend that throws anything", "[selection]")
{
  int made = 0;
  auto make = [&]<typename T>(T) -> std::unique_ptr<int> {
    if constexpr (std::is_same_v<T, throws>)
      throw 42; // Not a std::exception, like winrt::hresult_error.
    else
      return ++made, std::make_unique<int>(std::is_same_v<T, with_service> ? -1 : 7);
  };

  const auto result = libremidi::midi_any::make_first_available<int>(
      make, std::tuple{throws{}, with_service{}}, std::tuple{plain{}, plain{}});
  REQUIRE(result);
  CHECK(*result == 7);
  CHECK(made == 1);
}

TEST_CASE("the C API refuses a backend it cannot create", "[selection][c]")
{
  libremidi_api_configuration api;
  REQUIRE(libremidi_midi_api_configuration_init(&api) == 0);

  SECTION("observer")
  {
    libremidi_observer_configuration conf;
    REQUIRE(libremidi_midi_observer_configuration_init(&conf) == 0);
    api.configuration_type = libremidi_api_configuration::Observer;

    libremidi_midi_observer_handle* handle = nullptr;
    api.api = foreign_c_api;
    CHECK(libremidi_midi_observer_new(&conf, &api, &handle) == -ENOTSUP);
    CHECK(handle == nullptr);

    api.api = DUMMY;
    REQUIRE(libremidi_midi_observer_new(&conf, &api, &handle) == 0);
    REQUIRE(handle != nullptr);
    libremidi_midi_observer_free(handle);
  }

  SECTION("input")
  {
    libremidi_midi_configuration conf;
    REQUIRE(libremidi_midi_configuration_init(&conf) == 0);
    conf.version = libremidi_midi_configuration::MIDI1;
    conf.virtual_port = true;
    conf.port_name = "libremidi-selection-test";
    conf.on_midi1_message.callback
        = [](void*, libremidi_timestamp, const libremidi_midi1_symbol*, size_t) {};
    api.configuration_type = libremidi_api_configuration::Input;
    api.api = foreign_c_api;

    libremidi_midi_in_handle* handle = nullptr;
    CHECK(libremidi_midi_in_new(&conf, &api, &handle) == -ENOTSUP);
    CHECK(handle == nullptr);
  }

  SECTION("output")
  {
    libremidi_midi_configuration conf;
    REQUIRE(libremidi_midi_configuration_init(&conf) == 0);
    conf.version = libremidi_midi_configuration::MIDI1;
    conf.virtual_port = true;
    conf.port_name = "libremidi-selection-test";
    api.configuration_type = libremidi_api_configuration::Output;
    api.api = foreign_c_api;

    libremidi_midi_out_handle* handle = nullptr;
    CHECK(libremidi_midi_out_new(&conf, &api, &handle) == -ENOTSUP);
    CHECK(handle == nullptr);
  }
}
