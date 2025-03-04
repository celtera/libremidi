#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>

namespace libremidi::winmidi
{
class midi_in_impl final
    : public midi2::in_api
    , public error_handler
    , public winmidi_shared_data
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
      , m_session{
            configuration.context ? *configuration.context
                                  : MidiSession::Create(to_hstring(configuration.client_name))}
  {
    this->client_open_ = stdx::error{};
  }

  ~midi_in_impl() override
  {
    close_port();
    this->client_open_ = std::errc::not_connected;
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::WINDOWS_MIDI_SERVICES;
  }

  stdx::error open_port(const input_port& port, std::string_view) override
  {
    auto [ep, gp] = get_port(port.device_name, port.port);
    if (!ep || !gp)
      return std::errc::address_not_available;

    m_group_filter = port.port - 1;

    // TODO use a MidiGroupEndpointListener for the filtering
    m_endpoint = m_session.CreateEndpointConnection(ep.EndpointDeviceId());

    m_revoke_token = m_endpoint.MessageReceived(
        [this](
            const winrt::Microsoft::Windows::Devices::Midi2::IMidiMessageReceivedEventSource&,
            const winrt::Microsoft::Windows::Devices::Midi2::MidiMessageReceivedEventArgs& args) {
      process_message(args);
    });

    m_endpoint.Open();

    return stdx::error{};
  }

  void process_message(const winrt::Microsoft::Windows::Devices::Midi2::MidiMessageReceivedEventArgs& msg)
  {
    static constexpr timestamp_backend_info timestamp_info{
        .has_absolute_timestamps = true,
        .absolute_is_monotonic = false,
        .has_samples = false,
    };

    const auto& ump = msg.GetMessagePacket();
    auto pk = msg.PeekFirstWord();
    if (m_group_filter >= 0)
    {
      int group = cmidi2_ump_get_group(&pk);
      if (group != m_group_filter)
        return;
    }

    const auto& b = ump.GetAllWords();

    uint32_t ump_space[64];
    array_view<uint32_t> ref{ump_space};
    b.GetMany(0, ref);

    auto to_ns = [t = ump.Timestamp()] { return t; };
    m_processing.on_bytes(
        {ump_space, ump_space + b.Size()}, m_processing.timestamp<timestamp_info>(to_ns, 0));
  }

  stdx::error close_port() override
  {
    m_endpoint.MessageReceived(m_revoke_token);
    m_session.DisconnectEndpointConnection(m_endpoint.ConnectionId());
    return stdx::error{};
  }

  virtual timestamp absolute_timestamp() const noexcept override { return {}; }

private:
  MidiSession m_session;
  winrt::event_token m_revoke_token{};
  winrt::Microsoft::Windows::Devices::Midi2::MidiEndpointConnection m_endpoint{nullptr};
  midi2::input_state_machine m_processing{this->configuration};
  int m_group_filter = -1;
};
}
