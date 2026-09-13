#pragma once
#include <libremidi/backends/alsa_raw_ump/config.hpp>
#include <libremidi/backends/alsa_raw_ump/helpers.hpp>
#include <libremidi/backends/linux/alsa.hpp>
#include <libremidi/detail/midi_out.hpp>
#include <libremidi/error_handler.hpp>
#include <libremidi/output_configuration.hpp>

#include <alsa/asoundlib.h>

#include <cassert>
#include <cstdint>
#include <system_error>
#include <utility>

NAMESPACE_LIBREMIDI::alsa_raw_ump
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
    client_open_ = stdx::error{};
  }

  ~midi_out_impl() override
  {
    // Close a connection if it exists.
    midi_out_impl::close_port();
    client_open_ = std::errc::not_connected;
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_RAW_UMP; }

  stdx::error connect_port(const char* portname)
  {
    // A raw device is exclusive, and a blocking open of one already held waits
    // in the kernel rather than returning EBUSY. The non-blocking mode is
    // wanted for the open alone: writes must block until the device takes the
    // bytes, so it is cleared below.
    constexpr int mode = SND_RAWMIDI_SYNC | SND_RAWMIDI_NONBLOCK;
    int ret = snd.ump.open(NULL, &midiport_, portname, mode);
    if (ret < 0)
    {
      libremidi_handle_error(this->configuration, "cannot open device.");
      return from_errc(ret);
    }

    if (int rc = snd.ump.nonblock(midiport_, 0); rc < 0)
    {
      snd.ump.close(midiport_);
      midiport_ = nullptr;
      libremidi_handle_error(this->configuration, "cannot set device blocking.");
      return from_errc(rc);
    }
    return stdx::error{};
  }

  stdx::error open_port(const output_port& p, std::string_view) override
  {
    return connect_port(raw_from_port_handle(p.port).to_string().c_str());
  }

  stdx::error close_port() override
  {
    if (midiport_)
      snd.ump.close(midiport_);
    midiport_ = nullptr;
    return stdx::error{};
  }

  stdx::error send_ump(const uint32_t* ump_stream, std::size_t count) override
  {
    if (!midiport_)
      libremidi_handle_error(
          this->configuration,
          "trying to send a message without an open "
          "port.");

    return write(ump_stream, count * sizeof(uint32_t));
  }

  stdx::error write(const uint32_t* ump_stream, size_t bytes)
  {
    if (auto err = snd.ump.write(midiport_, ump_stream, bytes); err < 0)
    {
      libremidi_handle_error(this->configuration, "cannot write message.");
      return from_errc(err);
    }

    return stdx::error{};
  }

  snd_ump_t* midiport_{};
};

}
