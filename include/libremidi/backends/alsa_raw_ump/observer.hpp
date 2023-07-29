#pragma once
#include <libremidi/backends/alsa_raw/observer.hpp>

namespace libremidi::alsa_raw_ump
{

class observer_impl : public alsa_raw::observer_impl
{
  using alsa_raw::observer_impl::observer_impl;
  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_RAW_UMP; }
};

}
