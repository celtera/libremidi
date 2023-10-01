#pragma once
#include <libremidi/backends/alsa_raw/observer.hpp>
#include <libremidi/backends/alsa_raw_ump/helpers.hpp>

namespace libremidi::alsa_raw_ump
{

class observer_impl : public alsa_raw::observer_impl_base<midi2_enumerator>
{
  using alsa_raw::observer_impl_base<midi2_enumerator>::observer_impl_base;
  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_RAW_UMP; }
};

}
