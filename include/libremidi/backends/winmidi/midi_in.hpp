#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/session.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>
#include <libremidi/detail/ump_stream.hpp>

NAMESPACE_LIBREMIDI::winmidi
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
      , m_owner{configuration.context}
  {
    if (service_available()) this->client_open_ = stdx::error{};
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
    try
    {
      auto ep = get_port(port, port_direction::input);
      if (!ep)
        return std::errc::address_not_available;
      auto input = std::make_shared<port_receiver>(configuration, static_cast<int>(port.port));
      m_connection = ordinary_connection::acquire(
          m_owner.get(configuration.client_name), ep.EndpointDeviceId(), input);
      m_input = std::move(input);

      return stdx::error{};
    }
    catch (...)
    {
      return std::errc::io_error;
    }
  }

#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  stdx::error open_virtual_port(std::string_view port_name) override
  {
    // Create endpoint information for the virtual device
    using namespace midi2v;

    auto conf = setup_virtualdevice_config(configuration.client_name, port_name, port_name, MidiFunctionBlockDirection::BlockInput);

    m_virtual = MidiVirtualDeviceManager::CreateVirtualDevice(conf);
    if (m_virtual == nullptr)
      return std::errc::device_or_resource_busy;

    try
    {
      // Create a connection to the device-side endpoint
      m_session = m_owner.get(configuration.client_name);
      if (!m_session) return std::errc::not_connected;
      m_endpoint = m_session.CreateEndpointConnection(m_virtual.DeviceEndpointDeviceId());
      if (!m_endpoint)
        return std::errc::device_or_resource_busy;

      // Add the virtual device as a message processing plugin to receive messages
      m_endpoint.AddMessageProcessingPlugin(m_virtual);

      m_input = std::make_shared<port_receiver>(configuration, -1);
      m_revoke_token = m_endpoint.MessageReceived(
          [weak = std::weak_ptr{m_input}](const auto&, const auto& args) {
            if (auto input = weak.lock())
              receive_event(args, [&](auto words, auto ticks) { input->receive(words, ticks); });
          });

      m_endpoint.Open();

      return stdx::error{};
    }
    catch (...)
    {
      return std::errc::io_error;
    }
  }
#endif

  stdx::error close_port() override
  {
    if (m_input)
    {
      m_input->gate.stop();
      m_input.reset();
    }
    if (m_connection)
    {
      m_connection.reset();
      return stdx::error{};
    }
    if(!m_endpoint)
      return std::errc::not_connected;

    m_endpoint.MessageReceived(m_revoke_token);

    m_session.DisconnectEndpointConnection(m_endpoint.ConnectionId());
    m_endpoint = nullptr;
    m_session = nullptr;

#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
    if (m_virtual)
    {
      m_virtual.Cleanup();
      m_virtual = nullptr;
    }
  #endif
    return stdx::error{};
  }

  virtual timestamp absolute_timestamp() const noexcept override { return {}; }

private:
  session_handle m_owner;
  std::shared_ptr<ordinary_connection> m_connection;
  std::shared_ptr<port_receiver> m_input;
  MidiSession m_session{nullptr}; // private virtual-device connection only
  winrt::event_token m_revoke_token{};
  MidiEndpointConnection m_endpoint{nullptr};
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  midi2v::MidiVirtualDevice m_virtual{nullptr};
#endif
};
}
