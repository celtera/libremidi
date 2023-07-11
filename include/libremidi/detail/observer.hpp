#pragma once
#include <libremidi/libremidi.hpp>

namespace libremidi
{
class observer_api
{
public:
  explicit observer_api(observer::callbacks c)
      : callbacks_{std::move(c)}
  {
  }

  virtual ~observer_api() = default;

protected:
  observer::callbacks callbacks_;
};

}
