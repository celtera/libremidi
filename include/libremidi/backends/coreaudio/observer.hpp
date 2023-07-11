#pragma once
#include <libremidi/backends/coreaudio/config.hpp>
#include <libremidi/detail/observer.hpp>

namespace libremidi
{
class observer_core final : public observer_api
{
public:
  explicit observer_core(observer::callbacks&& c)
      : observer_api{std::move(c)}
  {
  }

  ~observer_core() { }
};
}
