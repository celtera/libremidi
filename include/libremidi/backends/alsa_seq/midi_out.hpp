#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi::alsa_seq
{

class midi_out_impl final
    : public midi1::out_api
    , private alsa_data
    , public error_handler
{
public:
  struct
      : libremidi::output_configuration
      , alsa_seq::output_configuration
  {
  } configuration;

  midi_out_impl(libremidi::output_configuration&& conf, alsa_seq::output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (init_client(configuration) < 0)
    {
      error<driver_error>(
          this->configuration,
          "midi_in_alsa::initialize: error creating ALSA sequencer client "
          "object.");
      return;
    }

    if (snd.midi.event_new(this->bufferSize, &this->coder) < 0)
    {
      error<driver_error>(
          this->configuration,
          "midi_out_alsa::initialize: error initializing MIDI event "
          "parser.");
      return;
    }
    snd.midi.event_init(this->coder);
  }

  ~midi_out_impl() override
  {
    // Close a connection if it exists.
    midi_out_impl::close_port();

    // Cleanup.
    if (this->vport >= 0)
      snd.seq.delete_port(this->seq, this->vport);
    if (this->coder)
      snd.midi.event_free(this->coder);

    if (!configuration.context)
      snd.seq.close(this->seq);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_SEQ; }

  [[nodiscard]] int create_port(std::string_view portName)
  {
    return alsa_data::create_port(
        *this, portName, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION, std::nullopt);
  }

  bool open_port(const output_port& p, std::string_view portName) override
  {
    unsigned int nSrc = this->get_port_count(SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
    if (nSrc < 1)
    {
      error<no_devices_found_error>(
          this->configuration, "midi_out_alsa::open_port: no MIDI output sources found!");
      return false;
    }

    auto sink = get_port_info(p);
    if (!sink)
      return false;

    if (int err = create_port(portName); err < 0)
    {
      error<driver_error>(configuration, "midi_out_alsa::create_port: ALSA error creating port.");
      return false;
    }

    snd_seq_addr_t source{
        .client = (unsigned char)snd.seq.client_id(this->seq), .port = (unsigned char)this->vport};
    if (int err = create_connection(*this, source, *sink, true); err < 0)
    {
      error<driver_error>(
          configuration, "midi_out_alsa::create_port: ALSA error making port connection.");
      return false;
    }

    return true;
  }

  bool open_virtual_port(std::string_view portName) override
  {
    if (int err = create_port(portName); err < 0)
      return false;
    return true;
  }

  void close_port() override { unsubscribe(); }

  void set_client_name(std::string_view clientName) override
  {
    alsa_data::set_client_name(clientName);
  }

  void set_port_name(std::string_view portName) override { alsa_data::set_port_name(portName); }

  void send_message(const unsigned char* message, std::size_t size) override
  {
    int64_t result{};
    if (size > this->bufferSize)
    {
      this->bufferSize = size;
      result = snd.midi.event_resize_buffer(this->coder, size);
      if (result != 0)
      {
        error<driver_error>(
            this->configuration,
            "midi_out_alsa::send_message: ALSA error resizing MIDI event "
            "buffer.");
        return;
      }
    }

    std::size_t offset = 0;
    while (offset < size)
    {
      snd_seq_event_t ev;
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_source(&ev, this->vport);
      snd_seq_ev_set_subs(&ev);
      // FIXME direct is set but snd_seq_event_output_direct is not used...
      snd_seq_ev_set_direct(&ev);

      const int64_t nBytes = size; // signed to avoir potential overflow with size - offset below
      result = snd.midi.event_encode(this->coder, message + offset, (long)(nBytes - offset), &ev);
      if (result < 0)
      {
        warning(this->configuration, "midi_out_alsa::send_message: event parsing error!");
        return;
      }

      if (ev.type == SND_SEQ_EVENT_NONE)
      {
        warning(this->configuration, "midi_out_alsa::send_message: incomplete message!");
        return;
      }

      offset += result;

      result = snd.seq.event_output(this->seq, &ev);
      if (result < 0)
      {
        warning(
            this->configuration,
            "midi_out_alsa::send_message: error sending MIDI message to port.");
        return;
      }
    }
    snd.seq.drain_output(this->seq);
  }

private:
  unsigned int bufferSize{32};
};
}
