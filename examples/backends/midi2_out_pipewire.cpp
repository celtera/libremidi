#include "backend_test_utils.hpp"

#include <libremidi/backends/pipewire_ump.hpp>
#include <libremidi/libremidi.hpp>

using api = libremidi::pipewire_ump::backend;

int main(void)
try
{
  std::cerr << "API: " << api::name << "\n";

  libremidi::observer_configuration obs_config{.track_any = true};
  api::midi_observer_configuration obs_api_config;
  libremidi::observer obs{obs_config, obs_api_config};

  auto ports = obs.get_output_ports();
  for (const auto& port : ports)
  {
    std::cerr << "Port: " << port.port_name << "\n";
  }

  libremidi::output_configuration out_config;
  api::midi_out_configuration out_api_config;
  libremidi::midi_out midiout{out_config, out_api_config};

  {
    midiout.open_virtual_port();

    for (int n = 0; n < 100; n++)
    {
      for (int i = 81; i < 89; i++)
        midiout.send_message(libremidi::channel_events::control_change(1, i, rand()));
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }

  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  exit(EXIT_FAILURE);
}
