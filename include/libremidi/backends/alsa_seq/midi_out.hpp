#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{
class midi_out_alsa final : public midi_out_api
{
public:
  explicit midi_out_alsa(std::string_view clientName)
  {
    // Set up the ALSA sequencer client.
    snd_seq_t* seq{};
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) < 0)
    {
      error<driver_error>(
          "midi_out_alsa::initialize: error creating ALSA sequencer client "
          "object.");
      return;
    }

    // Set client name.
    snd_seq_set_client_name(seq, clientName.data());

    // Save our api-specific connection information.
    data.seq = seq;
    data.vport = -1;
    data.bufferSize = 32;
    data.coder = nullptr;
    int result = snd_midi_event_new(data.bufferSize, &data.coder);
    if (result < 0)
    {
      error<driver_error>(
          "midi_out_alsa::initialize: error initializing MIDI event "
          "parser!\n\n");
      return;
    }
    snd_midi_event_init(data.coder);
  }

  ~midi_out_alsa() override
  {
    // Close a connection if it exists.
    midi_out_alsa::close_port();

    // Cleanup.
    if (data.vport >= 0)
      snd_seq_delete_port(data.seq, data.vport);
    if (data.coder)
      snd_midi_event_free(data.coder);
    snd_seq_close(data.seq);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::LINUX_ALSA; }

  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (connected_)
    {
      warning("midi_out_alsa::open_port: a valid connection already exists!");
      return;
    }

    unsigned int nSrc = this->get_port_count();
    if (nSrc < 1)
    {
      error<no_devices_found_error>("midi_out_alsa::open_port: no MIDI output sources found!");
      return;
    }

    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    if (portInfo(
            data.seq, pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, (int)portNumber)
        == 0)
    {
      error<invalid_parameter_error>(
          "midi_out_alsa::open_port: invalid 'portNumber' argument: "
          + std::to_string(portNumber));
      return;
    }

    snd_seq_addr_t sender{}, receiver{};
    receiver.client = snd_seq_port_info_get_client(pinfo);
    receiver.port = snd_seq_port_info_get_port(pinfo);
    sender.client = snd_seq_client_id(data.seq);

    if (data.vport < 0)
    {
      data.vport = snd_seq_create_simple_port(
          data.seq, portName.data(), SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
          SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
      if (data.vport < 0)
      {
        error<driver_error>("midi_out_alsa::open_port: ALSA error creating output port.");
        return;
      }
    }

    sender.port = data.vport;

    // Make subscription
    if (snd_seq_port_subscribe_malloc(&data.subscription) < 0)
    {
      snd_seq_port_subscribe_free(data.subscription);
      error<driver_error>("midi_out_alsa::open_port: error allocating port subscription.");
      return;
    }
    snd_seq_port_subscribe_set_sender(data.subscription, &sender);
    snd_seq_port_subscribe_set_dest(data.subscription, &receiver);
    snd_seq_port_subscribe_set_time_update(data.subscription, 1);
    snd_seq_port_subscribe_set_time_real(data.subscription, 1);
    if (snd_seq_subscribe_port(data.seq, data.subscription))
    {
      snd_seq_port_subscribe_free(data.subscription);
      error<driver_error>("midi_out_alsa::open_port: ALSA error making port connection.");
      return;
    }

    connected_ = true;
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (data.vport < 0)
    {
      data.vport = snd_seq_create_simple_port(
          data.seq, portName.data(), SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
          SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

      if (data.vport < 0)
      {
        error<driver_error>("midi_out_alsa::open_virtual_port: ALSA error creating virtual port.");
      }
    }
  }

  void close_port() override
  {
    if (connected_)
    {
      snd_seq_unsubscribe_port(data.seq, data.subscription);
      snd_seq_port_subscribe_free(data.subscription);
      data.subscription = nullptr;
      connected_ = false;
    }
  }

  void set_client_name(std::string_view clientName) override
  {
    snd_seq_set_client_name(data.seq, clientName.data());
  }

  void set_port_name(std::string_view portName) override
  {
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_get_port_info(data.seq, data.vport, pinfo);
    snd_seq_port_info_set_name(pinfo, portName.data());
    snd_seq_set_port_info(data.seq, data.vport, pinfo);
  }

  unsigned int get_port_count() override
  {
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    return portInfo(data.seq, pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, -1);
  }

  std::string get_port_name(unsigned int portNumber) override
  {
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);

    if (portInfo(
            data.seq, pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            (int)portNumber))
    {
      return portName(data.seq, pinfo);
    }

    // If we get here, we didn't find a match.
    warning("midi_out_alsa::get_port_name: error looking for port name!");
    return {};
  }

  void send_message(const unsigned char* message, std::size_t size) override
  {
    int64_t result{};
    if (size > data.bufferSize)
    {
      data.bufferSize = size;
      result = snd_midi_event_resize_buffer(data.coder, size);
      if (result != 0)
      {
        error<driver_error>(
            "midi_out_alsa::send_message: ALSA error resizing MIDI event "
            "buffer.");
        return;
      }
    }

    auto& buffer = data.buffer;
    buffer.assign(message, message + size);

    std::size_t offset = 0;
    while (offset < size)
    {
      snd_seq_event_t ev;
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_source(&ev, data.vport);
      snd_seq_ev_set_subs(&ev);
      snd_seq_ev_set_direct(&ev);

      const int64_t nBytes = size; // signed to avoir potential overflow with size - offset below
      result = snd_midi_event_encode(
          data.coder, data.buffer.data() + offset, (long)(nBytes - offset), &ev);
      if (result < 0)
      {
        warning("midi_out_alsa::send_message: event parsing error!");
        return;
      }

      if (ev.type == SND_SEQ_EVENT_NONE)
      {
        warning("midi_out_alsa::send_message: incomplete message!");
        return;
      }

      offset += result;

      result = snd_seq_event_output(data.seq, &ev);
      if (result < 0)
      {
        warning("midi_out_alsa::send_message: error sending MIDI message to port.");
        return;
      }
    }
    snd_seq_drain_output(data.seq);
  }

private:
  alsa_out_data data;
};
}
