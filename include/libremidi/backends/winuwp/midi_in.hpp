#pragma once
#include <libremidi/backends/winuwp/config.hpp>
#include <libremidi/backends/winuwp/helpers.hpp>
#include <libremidi/backends/winuwp/observer.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>

namespace libremidi
{

class midi_in_winuwp final
    : public midi1::in_api
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

  ~midi_in_winuwp() override { close_port(); }

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

  bool open_port(const input_port& port, std::string_view) override
  {
    const auto id = winrt::to_hstring(port.port_name);
    if (id.empty())
      return false;

    port_ = get(MidiInPort::FromIdAsync(id));
    if (!port_)
      return false;

    midi_start_timestamp = std::chrono::steady_clock::now();

    port_.MessageReceived(
        [=](const winrt::Windows::Devices::Midi::IMidiInPort& inputPort,
            const winrt::Windows::Devices::Midi::MidiMessageReceivedEventArgs& args) {
      this->process_message(args.Message());
    });

    return true;
  }

  void process_message(const winrt::Windows::Devices::Midi::IMidiMessage& msg)
  {
    static constexpr timestamp_backend_info timestamp_info{
        .has_absolute_timestamps = true,
        .absolute_is_monotonic = false,
        .has_samples = false,
    };

    auto reader = DataReader::FromBuffer(msg.RawData());
    auto begin = msg.RawData().data();
    auto end = begin + msg.RawData().Length();

    const auto to_ns = [&msg] { return msg.Timestamp().count() * 1'000'000; };
    m_processing.on_bytes({begin, end}, m_processing.timestamp<timestamp_info>(to_ns, 0));
  }

  void close_port() override
  {
    if (port_)
    {
      port_.Close();
      port_ = nullptr;
    }
  }

  timestamp absolute_timestamp() const noexcept override
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now() - midi_start_timestamp)
        .count();
  }

private:
  winrt::Windows::Devices::Midi::IMidiInPort port_{nullptr};
  std::chrono::steady_clock::time_point midi_start_timestamp;

  midi1::input_state_machine m_processing{this->configuration};
};
}
