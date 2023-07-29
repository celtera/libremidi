#pragma once
#include <libremidi/backends/coremidi/observer.hpp>

namespace libremidi::coremidi_ump
{

class observer_impl final : public libremidi::observer_core
{
  using observer_core::observer_core;
  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI_UMP; }
};

}
