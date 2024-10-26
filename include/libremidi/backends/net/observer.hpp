#pragma once
#include <libremidi/backends/dummy.hpp>

namespace libremidi::net
{
using net_observer_configuration = libremidi::dummy_configuration;
using observer = libremidi::observer_dummy;
}

namespace libremidi::net_ump
{
using net_observer_configuration = libremidi::dummy_configuration;
using observer = libremidi::observer_dummy;
}
