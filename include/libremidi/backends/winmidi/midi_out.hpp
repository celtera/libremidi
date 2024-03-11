#pragma once
#include <libremidi/detail/ump_stream.hpp>
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi::winmidi
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
      , m_session{MidiSession::CreateSession(L"libremidi session")}
  {
    this->client_open_ = stdx::error{};
  }

  ~midi_out_impl() override { close_port(); }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::WINDOWS_MIDI_SERVICES;
  }

  stdx::error open_port(const output_port& port, std::string_view) override
  {
    auto ep = get_port_by_name(port.port_name);
    if (!ep)
      return std::errc::address_not_available;

    m_endpoint = m_session.CreateEndpointConnection(ep.Id());
    m_endpoint.Open();

    return stdx::error{};
  }

  stdx::error close_port() override
  {
    m_session.DisconnectEndpointConnection(m_endpoint.ConnectionId());
    return stdx::error{};
  }

  stdx::error send_ump(const uint32_t* message, size_t size) override
  {
    auto write_func = [this](const uint32_t* ump, int64_t bytes) -> std::errc {
      MidiSendMessageResults ret{};
      switch(bytes / 4)
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
          ret = m_endpoint.SendSingleMessagePacket(MidiMessage128(0, ump[0], ump[1], ump[2], ump[3]));
          break;
        default:
          return std::errc::bad_message;
      }

      if(ret != MidiSendMessageResults::Succeeded)
        return std::errc::bad_message;
      return std::errc{0};
    };

    return segment_ump_stream(message, size, write_func, []() {});
  }

private:
  MidiSession m_session;
  winrt::Windows::Devices::Midi2::MidiEndpointConnection m_endpoint{nullptr};
};

}
