// A module that embeds libremidi, as an audio plug-in does. No port is opened:
// no session is created and the MIDI service is not needed.
#include <libremidi/backends/winmidi/midi_in.hpp>
#include <libremidi/backends/winmidi/midi_out.hpp>

#include <memory>

namespace w = libremidi::winmidi;

// Destroyed when the module is unloaded, from DllMain.
static std::unique_ptr<w::midi_in_impl> left_open;

extern "C" __declspec(dllexport) int libremidi_plugin_run()
try
{
  {
    w::midi_in_impl in{libremidi::ump_input_configuration{}, w::input_configuration{}};
    w::midi_out_impl out{libremidi::output_configuration{}, w::output_configuration{}};
  }
  left_open = std::make_unique<w::midi_in_impl>(
      libremidi::ump_input_configuration{}, w::input_configuration{});
  return 0;
}
catch (...)
{
  return 1;
}
