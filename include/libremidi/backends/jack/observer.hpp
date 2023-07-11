#pragma once
#include <libremidi/detail/observer.hpp>

namespace libremidi
{
class observer_jack final : public observer_api
{
public:
  explicit observer_jack(observer::callbacks&& c)
      : observer_api{std::move(c)}
  {
  }

  ~observer_jack() { }
};
}
