#pragma once
#include <libremidi/backends/winuwp/config.hpp>
#include <libremidi/backends/winuwp/helpers.hpp>
#include <libremidi/backends/winuwp/observer.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{

class midi_in_winuwp final
    : public midi_in_api
    , public error_handler
{
public:
  struct
      : input_configuration
      , winuwp_input_configuration
  {
  } configuration;

  explicit midi_in_winuwp(input_configuration&& conf, winuwp_input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}

  {
    winrt_init();
  }

  ~midi_in_winuwp() override
  {
    if (port_)
      port_.Close();
  }

  bool open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_in_winuwp: open_virtual_port unsupported");
    return false;
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_winuwp: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_in_winuwp: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_UWP; }

  bool open_port(const port_information& port, std::string_view) override
  {
    const auto id = winrt::to_hstring(port.port_name);
    if (id.empty())
      return false;

    port_ = get(MidiInPort::FromIdAsync(id));
    if (!port_)
        return false;

    port_.MessageReceived(
        [=](const winrt::Windows::Devices::Midi::IMidiInPort& inputPort,
            const winrt::Windows::Devices::Midi::MidiMessageReceivedEventArgs& args) {
      const auto& msg = args.Message();

      auto reader = DataReader::FromBuffer(msg.RawData());
      array_view<uint8_t> bs;
      reader.ReadBytes(bs);

      auto t = msg.Timestamp().count();
      this->configuration.on_message(libremidi::message{{bs.begin(), bs.end()}, t});
    });

    return true;
  }

  void close_port() override
  {
    if (port_)
      port_.Close();
    connected_ = false;
  }

private:
  hstring get_port_id(unsigned int portNumber)
  {
    auto& observer = observer_winuwp::get_internal_in_port_observer();
    return observer.get_port_id(portNumber);
  }

private:
  winrt::Windows::Devices::Midi::MidiInPort port_{nullptr};
};
}
