#pragma once
#include <libremidi/backends/winuwp/config.hpp>
#include <libremidi/backends/winuwp/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{

class midi_out_winuwp final : public midi_out_default<midi_out_winuwp>
{
public:
  static const constexpr auto backend = "UWP";
  struct
      : output_configuration
      , winuwp_output_configuration
  {
  } configuration;

  midi_out_winuwp(output_configuration&& conf, winuwp_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)} {}
  {
    winrt_init();
  }

  ~midi_out_winuwp() override { close_port(); }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_UWP; }

  void open_port(unsigned int portNumber, std::string_view) override
  {
    if (connected_)
    {
      warning("midi_out_winuwp::open_port: a valid connection already exists!");
      return;
    }

    const auto id = get_port_id(portNumber);
    if (!id.empty())
    {
      port_ = get(MidiOutPort::FromIdAsync(id));
    }
  }

  void close_port() override
  {
    if (connected_)
    {
      if (port_)
      {
        port_.Close();
      }
    }
  }

  unsigned int get_port_count() const override
  {
    auto& out_ports_observer = observer_winuwp::get_internal_out_port_observer();
    return static_cast<unsigned int>(out_ports_observer.get_ports().size());
  }

  std::string get_port_name(unsigned int portNumber) const override
  {
    auto& observer = observer_winuwp::get_internal_out_port_observer();
    return observer.get_port_name(portNumber);
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    if (!connected_)
      return;

    InMemoryRandomAccessStream str;
    DataWriter rb(str);
    rb.WriteBytes(
        winrt::array_view<const uint8_t>{(const uint8_t*)message, (const uint8_t*)message + size});
    port_.SendBuffer(rb.DetachBuffer());
  }

private:
  hstring get_port_id(unsigned int portNumber)
  {
    auto& observer = observer_winuwp::get_internal_out_port_observer();
    return observer.get_port_id(portNumber);
  }

private:
  winrt::Windows::Devices::Midi::IMidiOutPort port_{nullptr};
};

}
