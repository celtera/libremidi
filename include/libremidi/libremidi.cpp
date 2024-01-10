#if !defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.hpp>
#endif

#include <libremidi/backends.hpp>
#include <libremidi/detail/midi_api.hpp>

#include <cmath>

#include <array>
#include <thread>

namespace libremidi
{

LIBREMIDI_INLINE
std::string_view get_version() noexcept
{
  return LIBREMIDI_VERSION;
}

LIBREMIDI_INLINE std::string_view get_api_name(libremidi::API api)
{
  std::string_view ret;
  midi_any::for_backend(api, [&](auto& b) { ret = b.name; });
  return ret;
}

LIBREMIDI_INLINE std::string_view get_api_display_name(libremidi::API api)
{
  std::string_view ret;
  midi_any::for_backend(api, [&](auto& b) { ret = b.display_name; });
  return ret;
}

LIBREMIDI_INLINE libremidi::API get_compiled_api_by_name(std::string_view name)
{
  libremidi::API ret = libremidi::API::UNSPECIFIED;
  midi_any::for_all_backends([&](auto& b) {
    if (name == b.name)
      ret = b.API;
  });
  return ret;
}

[[nodiscard]] LIBREMIDI_INLINE std::vector<libremidi::API> available_apis() noexcept
{
  std::vector<libremidi::API> apis;
  midi1::for_all_backends([&](auto b) { apis.push_back(b.API); });
  return apis;
}

[[nodiscard]] LIBREMIDI_INLINE std::vector<libremidi::API> available_ump_apis() noexcept
{
  std::vector<libremidi::API> apis;
  midi2::for_all_backends([&](auto b) { apis.push_back(b.API); });
  return apis;
}

LIBREMIDI_INLINE midi_exception::~midi_exception() = default;
LIBREMIDI_INLINE no_devices_found_error::~no_devices_found_error() = default;
LIBREMIDI_INLINE invalid_device_error::~invalid_device_error() = default;
LIBREMIDI_INLINE memory_error::~memory_error() = default;
LIBREMIDI_INLINE invalid_parameter_error::~invalid_parameter_error() = default;
LIBREMIDI_INLINE invalid_use_error::~invalid_use_error() = default;
LIBREMIDI_INLINE driver_error::~driver_error() = default;
LIBREMIDI_INLINE system_error::~system_error() = default;
LIBREMIDI_INLINE thread_error::~thread_error() = default;

LIBREMIDI_INLINE
std::any midi_in_configuration_for(libremidi::API api)
{
  std::any ret;
  midi_any::for_backend(api, [&]<typename T>(T) {
    using conf_type = typename T::midi_in_configuration;
    ret = conf_type{};
  });
  return ret;
}

LIBREMIDI_INLINE
std::any midi_out_configuration_for(libremidi::API api)
{
  std::any ret;
  midi_any::for_backend(api, [&]<typename T>(T) {
    using conf_type = typename T::midi_out_configuration;
    ret = conf_type{};
  });
  return ret;
}

LIBREMIDI_INLINE
std::any observer_configuration_for(libremidi::API api)
{
  std::any ret;
  midi_any::for_backend(api, [&]<typename T>(T) {
    using conf_type = typename T::midi_observer_configuration;
    ret = conf_type{};
  });
  return ret;
}

LIBREMIDI_INLINE
std::any midi_in_configuration_for(const libremidi::observer& obs)
{
  return midi_in_configuration_for(obs.get_current_api());
}

LIBREMIDI_INLINE
std::any midi_out_configuration_for(const libremidi::observer& obs)
{
  return midi_out_configuration_for(obs.get_current_api());
}

LIBREMIDI_INLINE
std::optional<input_port> in_default_port(libremidi::API api) noexcept
try
{
  libremidi::observer obs{{}, observer_configuration_for(api)};
  if (auto ports = obs.get_input_ports(); !ports.empty())
    return ports.front();
  return std::nullopt;
}
catch (const std::exception& e)
{
  return std::nullopt;
}

LIBREMIDI_INLINE
std::optional<output_port> out_default_port(libremidi::API api) noexcept
try
{
  libremidi::observer obs{{}, observer_configuration_for(api)};
  if (auto ports = obs.get_output_ports(); !ports.empty())
    return ports.front();
  return std::nullopt;
}
catch (const std::exception& e)
{
  return std::nullopt;
}

namespace midi1
{
LIBREMIDI_INLINE
std::any in_default_configuration()
{
  return midi_in_configuration_for(default_api());
}

LIBREMIDI_INLINE
std::any out_default_configuration()
{
  return midi_out_configuration_for(default_api());
}

LIBREMIDI_INLINE
std::any observer_default_configuration()
{
  return observer_configuration_for(default_api());
}
}

namespace midi2
{
LIBREMIDI_INLINE
std::any in_default_configuration()
{
  return midi_in_configuration_for(default_api());
}

LIBREMIDI_INLINE
std::any out_default_configuration()
{
  return midi_out_configuration_for(default_api());
}

LIBREMIDI_INLINE
std::any observer_default_configuration()
{
  return observer_configuration_for(default_api());
}
}

}
