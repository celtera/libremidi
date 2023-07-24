#pragma once
#include <libremidi/config.hpp>

#include <cinttypes>
#include <cstdint>
#include <string>

extern "C" {
typedef struct _jack_client jack_client_t;
typedef uint32_t jack_nframes_t;
typedef int (*JackProcessCallback)(jack_nframes_t nframes, void* arg);
}

namespace libremidi
{
using jack_callback_function = std::function<void(int nframes)>;
struct jack_input_configuration
{
  std::string client_name;

  jack_client_t* context{};
  std::function<void(jack_callback_function)> set_process_func;
};

struct jack_output_configuration
{
  std::string client_name;

  jack_client_t* context{};
  std::function<void(jack_callback_function)> set_process_func;

  int32_t ringbuffer_size = 16384;
};

struct jack_observer_configuration
{
  std::string client_name;
  jack_client_t* context{};
};

}
