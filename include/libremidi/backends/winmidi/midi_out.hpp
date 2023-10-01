#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi::winmidi
{

class midi_out_impl final
    : public midi2::out_api
    , public error_handler
{
public:
  struct
      : libremidi::output_configuration
      , winmidi::output_configuration
  {
  } configuration;

  midi_out_impl(libremidi::output_configuration&& conf, winmidi::output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  }

  ~midi_out_impl() override { close_port(); }

  bool open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_out_winmidi: open_virtual_port unsupported");
    return false;
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_out_winmidi: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_out_winmidi: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::WINDOWS_MIDI_SERVICES;
  }

  bool open_port(const output_port& port, std::string_view) override
  {
#if 0
    const auto id = winrt::to_hstring(port.port_name);
    if (id.empty())
      return false;

    port_ = get(MidiOutPort::FromIdAsync(id));
    return bool(port_);
#endif
    return true;
  }

  void close_port() override
  {
#if 0
    if (port_)
    {
      port_.Close();
      port_ = {};
    }
#endif
  }

  void send_ump(const uint32_t* message, size_t size) override
  {
#if 0
    if (!port_)
      return;

    InMemoryRandomAccessStream str;
    DataWriter rb(str);
    rb.WriteBytes(
        winrt::array_view<const uint8_t>{(const uint8_t*)message, (const uint8_t*)message + size});
    port_.SendBuffer(rb.DetachBuffer());
#endif
  }

private:
#if 0
winrt::Windows::Devices::Midi2::IMidiOutPort port_{nullptr};
#endif
};

}
