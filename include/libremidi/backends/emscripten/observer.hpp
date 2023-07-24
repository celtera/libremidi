#pragma once
#include <libremidi/backends/emscripten/config.hpp>
#include <libremidi/backends/emscripten/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <vector>

namespace libremidi
{
class observer_emscripten final : public observer_api
{
  using device = webmidi_helpers::device_information;

public:
  struct
      : observer_configuration
      , emscripten_observer_configuration
  {
  } configuration;

  explicit observer_emscripten(observer_configuration&& conf, emscripten_observer_configuration&& apiconf);
  ~observer_emscripten();

  void
  update(const std::vector<device>& current_inputs, const std::vector<device>& current_outputs);

private:
  std::vector<device> m_known_inputs;
  std::vector<device> m_known_outputs;
};
}
