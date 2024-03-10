#pragma once
#include <libremidi/backends/coremidi_ump/config.hpp>
#include <libremidi/backends/coremidi/observer.hpp>

namespace libremidi::coremidi_ump
{

class observer_impl final : public libremidi::observer_core
{
public:
  explicit observer_impl(libremidi::observer_configuration&& conf, coremidi_ump::observer_configuration&& apiconf)
      : observer_core{std::move(conf), coremidi_observer_configuration{apiconf.client_name, apiconf.on_create_context}}
  {

  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI_UMP; }
};

}
