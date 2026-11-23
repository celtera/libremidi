#pragma once
#include <libremidi/backends/pipewire/config.hpp>
#include <libremidi/config.hpp>

NAMESPACE_LIBREMIDI::pipewire_ump
{
// Same borrow-mode contract as pipewire_input_configuration.
struct input_configuration
{
  std::string client_name = "libremidi client";

  pw_thread_loop* thread_loop{};
  pw_main_loop* main_loop{};
  pw_core* core{};
};

struct output_configuration
{
  std::string client_name = "libremidi client";

  pw_thread_loop* thread_loop{};
  pw_main_loop* main_loop{};
  pw_core* core{};

  // Bytes the output writes per cycle at most. PipeWire gives a MIDI input port a buffer
  // of 8192 bytes unless it asks for more, and an input drops a whole cycle that is
  // larger than its buffer: only raise this for receivers known to use larger buffers.
  int64_t output_buffer_size{8192};
};

struct observer_configuration
{
  std::string client_name = "libremidi client";

  pw_thread_loop* thread_loop{};
  pw_main_loop* main_loop{};
  pw_core* core{};
};
}
