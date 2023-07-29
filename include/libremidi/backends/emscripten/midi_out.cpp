#if defined(__EMSCRIPTEN__)
  #include <libremidi/backends/emscripten/midi_access.hpp>
  #include <libremidi/backends/emscripten/midi_out.hpp>

namespace libremidi
{
LIBREMIDI_INLINE midi_out_emscripten::midi_out_emscripten(
    output_configuration&& conf, emscripten_output_configuration&& apiconf)
    : configuration{std::move(conf), std::move(apiconf)}
{
}

LIBREMIDI_INLINE midi_out_emscripten::~midi_out_emscripten()
{
  // Close a connection if it exists.
  midi_out_emscripten::close_port();
}

LIBREMIDI_INLINE libremidi::API midi_out_emscripten::get_current_api() const noexcept
{
  return libremidi::API::WEBMIDI;
}

LIBREMIDI_INLINE bool midi_out_emscripten::open_port(unsigned int portNumber, std::string_view)
{
  auto& midi = webmidi_helpers::midi_access_emscripten::instance();

  if (portNumber >= midi.output_count())
  {
    error<no_devices_found_error>(
        this->configuration, "midi_out_emscripten::open_port: no MIDI output sources found.");
    return false;
  }

  portNumber_ = portNumber;
  return true;
}

LIBREMIDI_INLINE bool midi_out_emscripten::open_port(const output_port& p, std::string_view nm)
{
  return open_port(p.port, nm);
}

LIBREMIDI_INLINE void midi_out_emscripten::close_port() { }

LIBREMIDI_INLINE void midi_out_emscripten::set_client_name(std::string_view clientName)
{
  warning(configuration, "midi_out_emscripten::set_client_name: unsupported.");
}

LIBREMIDI_INLINE void midi_out_emscripten::set_port_name(std::string_view portName)
{
  warning(configuration, "midi_out_emscripten::set_port_name: unsupported.");
}

LIBREMIDI_INLINE bool midi_out_emscripten::open_virtual_port(std::string_view)
{
  warning(configuration, "midi_in_emscripten::open_virtual_port: unsupported.");
  return false;
}

LIBREMIDI_INLINE void midi_out_emscripten::send_message(const unsigned char* message, size_t size)
{
  if (portNumber_ < 0)
    error<invalid_use_error>(
        this->configuration,
        "midi_out_emscripten::send_message: trying to send a message without an open "
        "port.");

  webmidi_helpers::midi_access_emscripten::instance().send_message(
      portNumber_, reinterpret_cast<const char*>(message), size);
}
}
#endif
