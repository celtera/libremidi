#pragma once
#include <libremidi/libremidi.hpp>

namespace libremidi
{
class observer_api
{
public:
  virtual ~observer_api() = default;
};

template <typename T, typename Arg>
std::unique_ptr<observer_api> make(libremidi::observer_configuration&& conf, Arg&& arg)
{
  return std::make_unique<T>(std::move(conf), std::move(arg));
}
}
