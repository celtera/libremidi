#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/session.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/backends/winmidi/virtual_port.hpp>
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
    try
    {
      auto err = m_virtual.open(
          m_owner.get(configuration.client_name), configuration.client_name, port_name,
          configuration.virtual_endpoint_id, MidiFunctionBlockDirection::BlockOutput);
      if (!err.is_set())
        converter.reset();
      return err;
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
      m_connection.reset();
      return stdx::error{};
    }
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
    if (m_virtual)
    {
      m_virtual.close();
      return {};
    }
#endif
    return std::errc::not_connected;
  }

  static std::errc write(const MidiEndpointConnection& endpoint, const uint32_t* ump, int64_t bytes)
  {
    const auto words = static_cast<uint8_t>(bytes / sizeof(uint32_t));
    const auto result = endpoint.SendSingleMessageWordArray(0, 0, words, {ump, ump + words});
    if (!MidiEndpointConnection::SendMessageSucceeded(result))
      return std::errc::io_error;
    return std::errc{};
  }

  stdx::error send_ump(const uint32_t* message, size_t size) override
  {
    if (m_connection)
    {
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      return m_connection->send({message, size});
#else
      return segment_ump_stream(message, size, [this](const uint32_t* ump, int64_t bytes) {
        return write(m_connection->endpoint, ump, bytes);
      });
#endif
    }
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
    if (m_virtual)
      return segment_ump_stream(message, size, [this](const uint32_t* ump, int64_t bytes) {
        return write(m_virtual.endpoint(), ump, bytes);
      });
#endif
    return std::errc::not_connected;
  }

private:
  session_handle m_owner;
  std::shared_ptr<ordinary_connection> m_connection;
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  virtual_port m_virtual;
#endif
};

}
