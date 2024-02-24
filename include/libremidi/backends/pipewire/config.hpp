#pragma once
#include <libremidi/config.hpp>

#include <cinttypes>
#include <cstdint>
#include <functional>
#include <string>

extern "C" {
struct pw_main_loop;
}

namespace libremidi
{
using pipewire_callback_function = std::function<void(int nframes)>;
struct pipewire_callback
{
  int64_t token;
  std::function<void(int nframes)> callback;
};
struct pipewire_input_configuration
{
  std::string client_name = "libremidi client";

  pw_main_loop* context{};
  std::function<void(pipewire_callback)> set_process_func;
  std::function<void(int64_t)> clear_process_func;
};

struct pipewire_output_configuration
{
  std::string client_name = "libremidi client";

  pw_main_loop* context{};
  std::function<void(pipewire_callback)> set_process_func;
  std::function<void(int64_t)> clear_process_func;

  int64_t output_buffer_size{65536};
};

struct pipewire_observer_configuration
{
  std::string client_name = "libremidi client";

  pw_main_loop* context{};
};

}
