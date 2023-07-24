#pragma once
#include <libremidi/detail/observer.hpp>
#include <libremidi/backends/jack/config.hpp>

namespace libremidi
{
class observer_jack final : public observer_api
{
public:
  struct
      : observer_configuration
      , jack_observer_configuration
  {
  } configuration;

  explicit observer_jack(observer_configuration&& conf, jack_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  }

  ~observer_jack() { }
};
}
