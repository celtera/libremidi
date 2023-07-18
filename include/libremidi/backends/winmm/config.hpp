#pragma once
#include <libremidi/libremidi.hpp>

#define RT_WINMM_OBSERVER_POLL_PERIOD_MS 100

#define RT_SYSEX_BUFFER_SIZE 1024
#define RT_SYSEX_BUFFER_COUNT 4

namespace libremidi
{

struct winmm_input_configuration
{
};

struct winmm_output_configuration
{
};

}
