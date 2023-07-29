#pragma once
#include <libremidi/backends/winuwp/config.hpp>
#include <libremidi/backends/winuwp/helpers.hpp>
#include <libremidi/backends/winuwp/observer.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{

class midi_out_winuwp final
    : public midi1::out_api
    , public error_handler
{
public:
  struct
      : output_configuration
      , winuwp_output_configuration
  {
  } configuration;

  midi_out_winuwp(output_configuration&& conf, winuwp_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    winrt_init();
  }

  ~midi_out_winuwp() override { close_port(); }

  bool open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_out_winuwp: open_virtual_port unsupported");
    return false;
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_out_winuwp: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_out_winuwp: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_UWP; }

  bool open_port(const output_port& port, std::string_view) override
  {
    const auto id = winrt::to_hstring(port.port_name);
    if (id.empty())
      return false;

    port_ = get(MidiOutPort::FromIdAsync(id));
    return bool(port_);
  }

  void close_port() override
  {
    if (port_)
    {
      port_.Close();
      port_ = {};
    }
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    if (!port_)
      return;

    InMemoryRandomAccessStream str;
    DataWriter rb(str);
    rb.WriteBytes(
        winrt::array_view<const uint8_t>{(const uint8_t*)message, (const uint8_t*)message + size});
    port_.SendBuffer(rb.DetachBuffer());
  }

private:
  winrt::Windows::Devices::Midi::IMidiOutPort port_{nullptr};
};

}
