#pragma once
#include <libremidi/backends/coremidi/config.hpp>
#include <libremidi/ump_endpoint_configuration.hpp>

NAMESPACE_LIBREMIDI::coremidi_ump
{

struct endpoint_api_configuration
{
  std::string client_name = "libremidi client";
  std::optional<MIDIClientRef> context{};

  ump_callback on_message{};
  raw_ump_callback on_raw_data{};
};

struct endpoint_observer_api_configuration
{
  std::string client_name = "libremidi observer";
  std::function<void(MIDIClientRef)> on_create_context{};
};

}
