#include <libremidi/backends.hpp>
#include <libremidi/libremidi.hpp>

#if defined(_WIN32) && __has_include(<winrt/base.h>)
  #include <winrt/base.h>
#endif

#include <iostream>
#include <mutex>

int main()
try
{
#if defined(_WIN32) && __has_include(<winrt/base.h>)
  // Necessary for using WinUWP and WinMIDI, must be done as early as possible in your main()
  winrt::init_apartment();
#endif

  using namespace libremidi;
  namespace lm2 = libremidi::midi2;

  // Create a midi out
  midi_out midiout{{}, lm2::out_default_configuration()};
  if (auto err = midiout.open_virtual_port("my-midiout"); err != stdx::error{})
    err.throw_exception();

  // Create a midi in
  auto on_ump = [&](const libremidi::ump& message) {
    if (midiout.is_port_connected())
      midiout.send_ump(message);
  };
  midi_in midiin{{.on_message = on_ump}, lm2::in_default_configuration()};

  if (auto err = midiin.open_virtual_port("my-midiin"); err != stdx::error{})
    err.throw_exception();

  // Wait until we exit
  char input;
  std::cin.get(input);
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
