#if defined(__EMSCRIPTEN__)
  #include <libremidi/backends/emscripten/midi_access.hpp>
  #include <libremidi/backends/emscripten/observer.hpp>

namespace libremidi
{
LIBREMIDI_INLINE observer_emscripten::observer_emscripten(observer_configuration&& conf, emscripten_observer_configuration&& apiconf)
    : configuration{std::move(conf), std::move(apiconf)}
{
  webmidi_helpers::midi_access_emscripten::instance().register_observer(*this);
}

LIBREMIDI_INLINE observer_emscripten::~observer_emscripten()
{
  webmidi_helpers::midi_access_emscripten::instance().unregister_observer(*this);
}

LIBREMIDI_INLINE void observer_emscripten::update(
    const std::vector<observer_emscripten::device>& current_inputs,
    const std::vector<observer_emscripten::device>& current_outputs)
{
  // WebMIDI never remove inputs, it just marks them as disconnected.
  // At least in known browsers...
  assert(current_inputs.size() >= m_known_inputs.size());
  assert(current_outputs.size() >= m_known_outputs.size());

  for (std::size_t i = m_known_inputs.size(); i < current_inputs.size(); i++)
  {
    m_known_inputs.push_back(current_inputs[i]);
    configuration.input_added(i, m_known_inputs[i].name);
  }

  for (std::size_t i = m_known_outputs.size(); i < current_outputs.size(); i++)
  {
    m_known_outputs.push_back(current_outputs[i]);
    configuration.output_added(i, m_known_outputs[i].name);
  }
}
}
#endif
