#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/session.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/detail/midi_out.hpp>
#include <libremidi/detail/ump_stream.hpp>

NAMESPACE_LIBREMIDI::winmidi
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
      , m_owner{configuration.context}
  {
    if (service_available()) this->client_open_ = stdx::error{};
  }

  ~midi_out_impl() override { close_port(); }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::WINDOWS_MIDI_SERVICES;
  }

  stdx::error open_port(const output_port& port, std::string_view) override
  {
    try
    {
      auto ep = get_port(port, port_direction::output);
      if (!ep)
        return std::errc::address_not_available;
      m_connection = m_owner.use(configuration.client_name, [&](const MidiSession& session) {
        return ordinary_connection::acquire(session, ep.EndpointDeviceId());
      });
      m_endpoint = m_connection->endpoint;
      converter.reset(static_cast<uint8_t>(port.port));

      return stdx::error{};
    }
    catch (const endpoint_busy&)
    {
      // Opened from a receive callback while this endpoint is opening or closing.
      return std::errc::resource_unavailable_try_again;
    }
    catch (...)
    {
      close_port();
      return std::errc::io_error;
    }
  }

#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  stdx::error open_virtual_port(std::string_view port_name) override
  {
    // Create endpoint information for the virtual device
    using namespace midi2v;

    auto conf = setup_virtualdevice_config(configuration.client_name, port_name, port_name, MidiFunctionBlockDirection::BlockOutput);

    m_virtual = MidiVirtualDeviceManager::CreateVirtualDevice(conf);
    if (m_virtual == nullptr)
      return std::errc::device_or_resource_busy;

    try
    {
      m_session = m_owner.get(configuration.client_name);
      if (!m_session) return std::errc::not_connected;
      m_endpoint = m_session.CreateEndpointConnection(m_virtual.DeviceEndpointDeviceId());
      if (!m_endpoint)
      {
        close_port();
        return std::errc::device_or_resource_busy;
      }

      m_endpoint.AddMessageProcessingPlugin(m_virtual);

      if (!m_endpoint.Open())
      {
        close_port();
        return std::errc::io_error;
      }

      converter.reset();

      return stdx::error{};
    }
    catch (...)
    {
      close_port();
      return std::errc::io_error;
    }
  }
#endif

  // Also releases what a failed open left behind.
  stdx::error close_port() override
  {
    if (m_connection)
    {
      m_endpoint = nullptr;
      m_connection.reset();
      return stdx::error{};
    }

    const bool connected{m_endpoint};
    if (connected)
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
    if (!connected)
      return std::errc::not_connected;
    return stdx::error{};
  }

  std::errc write(const uint32_t* ump, int64_t bytes)
  {
    MidiSendMessageResults ret{};
    switch (bytes / 4)
    {
      case 1:
        ret = m_endpoint.SendSingleMessagePacket(MidiMessage32(0, ump[0]));
        break;
      case 2:
        ret = m_endpoint.SendSingleMessagePacket(MidiMessage64(0, ump[0], ump[1]));
        break;
      case 3:
        ret = m_endpoint.SendSingleMessagePacket(MidiMessage96(0, ump[0], ump[1], ump[2]));
        break;
      case 4:
        ret = m_endpoint.SendSingleMessagePacket(
            MidiMessage128(0, ump[0], ump[1], ump[2], ump[3]));
        break;
      default:
        return std::errc::bad_message;
    }
    if (ret != MidiSendMessageResults::Succeeded)
      return std::errc::io_error;
    return std::errc{};
  }

  stdx::error send_ump(const uint32_t* message, size_t size) override
  {
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
    // The virtual-device host uses the WinRT plugin path, not a raw connection.
    if (m_connection)
      return m_connection->send({message, size});
#endif
    return segment_ump_stream(message, size, [this](const uint32_t* ump, int64_t bytes) {
      return write(ump, bytes);
    });
  }

private:
  session_handle m_owner;
  std::shared_ptr<ordinary_connection> m_connection;
  MidiSession m_session{nullptr}; // private virtual-device connection only
  MidiEndpointConnection m_endpoint{nullptr};
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  midi2v::MidiVirtualDevice m_virtual{nullptr};
#endif
};

}
