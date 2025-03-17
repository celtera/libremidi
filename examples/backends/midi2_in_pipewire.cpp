#include "backend_test_utils.hpp"

#include <libremidi/backends/pipewire_ump.hpp>
#include <libremidi/libremidi.hpp>

using api = libremidi::pipewire_ump::backend;

int main(void)
try
{
  std::cerr << "API: " << api::name << "\n";

  libremidi::observer_configuration obs_config{.track_any = false};
  api::midi_observer_configuration obs_api_config;
  libremidi::observer obs{obs_config, obs_api_config};

  auto ports = obs.get_input_ports();
  for (const auto& port : ports)
  {
    std::cerr << "Port: " << port.display_name << " : " << port.port_name << "; "
              << port.device_name << "\n";
  }

  {
    libremidi::ump_input_configuration in_config;
    in_config.on_message = [](const libremidi::ump& m) { std::cerr << m << "\n"; };
    api::midi_in_configuration in_api_config;
    libremidi::midi_in midiin{in_config, in_api_config};

    if (!ports.empty())
    {
      midiin.open_port(ports[0]);
      std::this_thread::sleep_for(std::chrono::seconds(50));
    }
  }

  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  exit(EXIT_FAILURE);
}
