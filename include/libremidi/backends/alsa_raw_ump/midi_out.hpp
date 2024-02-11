#pragma once
#include <libremidi/backends/alsa_raw_ump/config.hpp>
#include <libremidi/backends/alsa_raw_ump/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

#include <alsa/asoundlib.h>

#include <atomic>
#include <thread>

namespace libremidi::alsa_raw_ump
{
class midi_out_impl final
    : public midi2::out_api
    , public error_handler
{
public:
  struct
      : libremidi::output_configuration
      , libremidi::alsa_raw_ump::output_configuration
  {
  } configuration;

  const libasound& snd = libasound::instance();

  midi_out_impl(
      libremidi::output_configuration&& conf,
      libremidi::alsa_raw_ump::output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    assert(snd.ump.available);
  }

  ~midi_out_impl() override
  {
    // Close a connection if it exists.
    midi_out_impl::close_port();
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_RAW; }

  bool open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_out_alsa_raw: open_virtual_port unsupported");
    return false;
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_out_alsa_raw: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_out_alsa_raw: set_port_name unsupported");
  }

  int connect_port(const char* portname)
  {
    constexpr int mode = SND_RAWMIDI_SYNC;
    int status = snd.ump.open(NULL, &midiport_, portname, mode);
    if (status < 0)
    {
      error<driver_error>(
          this->configuration, "midi_out_alsa_raw::open_port: cannot open device.");
      return status;
    }
    return status;
  }

  bool open_port(const output_port& p, std::string_view) override
  {
    return connect_port(raw_from_port_handle(p.port).to_string().c_str()) == 0;
  }

  void close_port() override
  {
    if (midiport_)
      snd.ump.close(midiport_);
    midiport_ = nullptr;
  }

  void send_ump(const uint32_t* ump_stream, std::size_t count) override
  {
    if (!midiport_)
      error<invalid_use_error>(
          this->configuration,
          "midi_out_alsa_raw::send_message: trying to send a message without an open "
          "port.");

    write(ump_stream, count * sizeof(uint32_t));
  }

  bool write(const uint32_t* ump_stream, size_t bytes)
  {
    if (snd.ump.write(midiport_, ump_stream, bytes) < 0)
    {
      error<driver_error>(
          this->configuration, "midi_out_alsa_raw::send_message: cannot write message.");
      return false;
    }

    return true;
  }

  snd_ump_t* midiport_{};
};

}
