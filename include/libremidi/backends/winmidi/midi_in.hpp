#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/session.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/backends/winmidi/virtual_port.hpp>
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
      m_connection = m_owner.use(configuration.client_name, [&](const MidiSession& session) {
        return ordinary_connection::acquire(session, ep.EndpointDeviceId(), input);
      });
      m_input = std::move(input);

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
      auto input = std::make_shared<port_receiver>(configuration, 0);
      auto err = m_virtual.open(
          m_owner.get(configuration.client_name), configuration.client_name, port_name,
          configuration.virtual_endpoint_id, MidiFunctionBlockDirection::BlockInput,
          [weak = std::weak_ptr{input}](const auto&, const auto& args) {
            if (auto active = weak.lock())
              receive_event(args, [&](auto words, auto ticks) { active->receive(words, ticks); });
          });
      if (err == stdx::error{})
        m_input = std::move(input);
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
    if (m_input)
    {
      m_input->gate.stop();
      if (m_connection)
        m_connection->detach(m_input);
      m_input.reset();
    }
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

  virtual timestamp absolute_timestamp() const noexcept override { return {}; }

private:
  session_handle m_owner;
  std::shared_ptr<ordinary_connection> m_connection;
  std::shared_ptr<port_receiver> m_input;
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  virtual_port m_virtual;
#endif
};
}
