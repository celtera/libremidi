#if defined(__EMSCRIPTEN__)
  #include <libremidi/backends/emscripten/midi_access.hpp>
  #include <libremidi/backends/emscripten/midi_in.hpp>

namespace libremidi
{
LIBREMIDI_INLINE midi_in_emscripten::midi_in_emscripten(
    input_configuration&& conf, emscripten_input_configuration&& apiconf)
    : configuration{std::move(conf), std::move(apiconf)}
{
}

LIBREMIDI_INLINE midi_in_emscripten::~midi_in_emscripten()
{
  // Close a connection if it exists.
  midi_in_emscripten::close_port();
}

LIBREMIDI_INLINE libremidi::API midi_in_emscripten::get_current_api() const noexcept
{
  return libremidi::API::EMSCRIPTEN_WEBMIDI;
}

LIBREMIDI_INLINE void midi_in_emscripten::open_port(unsigned int portNumber, std::string_view)
{
  auto& midi = webmidi_helpers::midi_access_emscripten::instance();

  if (portNumber >= midi.input_count())
  {
    error<no_devices_found_error>(
        this->configuration, "midi_in_emscripten::open_port: no MIDI output sources found.");
    return;
  }

  midi.open_input(portNumber, *this);
  portNumber_ = portNumber;
  connected_ = true;
}

LIBREMIDI_INLINE void
midi_in_emscripten::open_port(const libremidi::port_information& p, std::string_view nm)
{
  return open_port(p.port, nm);
}

LIBREMIDI_INLINE void midi_in_emscripten::open_virtual_port(std::string_view)
{
  warning(configuration, "midi_in_emscripten::open_virtual_port: unsupported.");
}

LIBREMIDI_INLINE void midi_in_emscripten::close_port()
{
  if (connected_)
  {
    auto& midi = webmidi_helpers::midi_access_emscripten::instance();

    midi.close_input(portNumber_, *this);
    connected_ = false;
  }
}

LIBREMIDI_INLINE void midi_in_emscripten::set_client_name(std::string_view clientName)
{
  warning(configuration, "midi_in_emscripten::set_client_name: unsupported.");
}

LIBREMIDI_INLINE void midi_in_emscripten::set_port_name(std::string_view portName)
{
  warning(configuration, "midi_in_emscripten::set_port_name: unsupported.");
}

LIBREMIDI_INLINE void midi_in_emscripten::set_timestamp(double ts, libremidi::message& m)
{
  switch (configuration.timestamps)
  {
    case input_configuration::NoTimestamp:
      m.timestamp = 0;
      break;
    case input_configuration::Relative: {
      if (firstMessage == true)
      {
        firstMessage = false;
        m.timestamp = 0;
      }
      else
      {
        m.timestamp = (ts - last_time_) * 1e6;
      }
      last_time_ = ts;
      break;
    }
    case input_configuration::Absolute:
    case input_configuration::SystemMonotonic:
      m.timestamp = ts * 1e6;
      break;
  }
}

LIBREMIDI_INLINE void midi_in_emscripten::on_input(libremidi::message msg)
{
  this->configuration.on_message(std::move(msg));
}

}
#endif
