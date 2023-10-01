#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi::winmidi
{

class midi_in_impl final
    : public midi2::in_api
    , public error_handler
{
public:
  struct
      : libremidi::ump_input_configuration
      , winmidi::input_configuration
  {
  } configuration;

  explicit midi_in_impl(
      libremidi::ump_input_configuration&& conf, winmidi::input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  }

  ~midi_in_impl() override { close_port(); }

  bool open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_in_winmidi: open_virtual_port unsupported");
    return false;
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_winmidi: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_in_winmidi: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::WINDOWS_MIDI_SERVICES;
  }

  bool open_port(const input_port& port, std::string_view) override
  {
#if 0
    const auto id = winrt::to_hstring(port.port_name);
    if (id.empty())
      return false;

    port_ = get(MidiInPort::FromIdAsync(id));
    if (!port_)
      return false;
    port_.MessageReceived(
        [=](const winrt::Windows::Devices::Midi::IMidiInPort& inputPort,
            const winrt::Windows::Devices::Midi::MidiMessageReceivedEventArgs& args) {
      this->process_message(args.Message());
    });

#endif
    return true;
  }

#if 0
  void process_message(const winrt::Windows::Devices::Midi::IMidiMessage& msg)
  {
    auto reader = DataReader::FromBuffer(msg.RawData());
    auto begin = msg.RawData().data();
    auto end = begin + msg.RawData().Length();

    auto t = msg.Timestamp().count();
    this->configuration.on_message(libremidi::message{{begin, end}, t});
  }
#endif

  void close_port() override
  {
#if 0
    if (port_)
    {
      port_.Close();
      port_ = nullptr;
    }
#endif
  }

private:
  // winrt::Microsoft::Devices::Midi2::IMidiInPort port_{nullptr};
};
}
