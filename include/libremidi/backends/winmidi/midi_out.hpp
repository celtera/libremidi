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
      // The MIDI 1 converter, unlike an explicit UMP, needs a destination group.
      converter.context.group = static_cast<uint8_t>(port.port);
      m_connection = ordinary_connection::acquire(
          m_owner.get(configuration.client_name), ep.EndpointDeviceId());
      m_endpoint = m_connection->endpoint;
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      m_raw_endpoint = m_connection->raw;
#endif

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

    converter.context.group = 0;
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
        return std::errc::device_or_resource_busy;

      m_endpoint.AddMessageProcessingPlugin(m_virtual);

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
    if (m_connection)
    {
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      m_raw_endpoint = nullptr;
#endif
      m_endpoint = nullptr;
      m_connection.reset();
      return stdx::error{};
    }
    if(!m_endpoint)
      return std::errc::not_connected;

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

  std::errc write_raw(const uint32_t* ump, int64_t bytes)
  {
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
    HRESULT ret{};
    switch (bytes / 4)
    {
      case 1:
        assert(m_raw_endpoint->ValidateBufferHasOnlyCompleteUmps(1, ump));
        ret = m_raw_endpoint->SendMidiMessagesRaw(0, 1, ump);
        break;
      case 2:
        assert(m_raw_endpoint->ValidateBufferHasOnlyCompleteUmps(2, ump));
        ret = m_raw_endpoint->SendMidiMessagesRaw(0, 2, ump);
        break;
      case 3:
        assert(m_raw_endpoint->ValidateBufferHasOnlyCompleteUmps(3, ump));
        ret = m_raw_endpoint->SendMidiMessagesRaw(0, 3, ump);
        break;
      case 4:
        assert(m_raw_endpoint->ValidateBufferHasOnlyCompleteUmps(4, ump));
        ret = m_raw_endpoint->SendMidiMessagesRaw(0, 4, ump);
        break;
      default:
        return std::errc::bad_message;
    }
    if(ret < 0)
      return std::errc::io_error;
    return std::errc{};
  #else
    return write(ump, bytes);
  #endif
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
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
    if(m_virtual)
    {
      // Virtual port does not support raw API per
      // https://github.com/celtera/libremidi/issues/194#issuecomment-4156127901

      return segment_ump_stream(message, size,
                                [this](const uint32_t* ump, int64_t bytes) -> std::errc {
        return write(ump, bytes);
      }, []() { });
    }
    else
#endif
    {
      return segment_ump_stream(message, size,
                                [this](const uint32_t* ump, int64_t bytes) -> std::errc {
        return write_raw(ump, bytes);
      }, []() { });
    }
  }

private:
  session_handle m_owner;
  std::shared_ptr<ordinary_connection> m_connection;
  MidiSession m_session{nullptr}; // private virtual-device connection only
  MidiEndpointConnection m_endpoint{nullptr};
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
  winrt::impl::com_ref<IMidiEndpointConnectionRaw> m_raw_endpoint{};
#endif
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  midi2v::MidiVirtualDevice m_virtual{nullptr};
#endif
};

}
