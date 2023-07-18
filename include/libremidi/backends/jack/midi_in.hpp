#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
class midi_in_jack final
    : public midi_in_api
    , private jack_helpers
{
public:
  explicit midi_in_jack(std::string_view cname)
      : midi_in_api{&data}
  {
    // TODO do like the others
    data.port = nullptr;
    data.client = nullptr;
    this->clientName = cname;

    connect();
  }

  ~midi_in_jack() override
  {
    midi_in_jack::close_port();

    if (data.client)
      jack_client_close(data.client);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::UNIX_JACK; }

  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (!check_port_name_length(*this, clientName, portName))
      return;

    connect();

    // Creating new port
    if (data.port == nullptr)
      data.port = jack_port_register(
          data.client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (data.port == nullptr)
    {
      error<driver_error>("midi_in_jack::open_port: JACK error creating port");
      return;
    }

    // Connecting to the output
    std::string name = get_port_name(portNumber);
    jack_connect(data.client, name.c_str(), jack_port_name(data.port));

    connected_ = true;
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (!check_port_name_length(*this, clientName, portName))
      return;

    connect();
    if (!data.port)
      data.port = jack_port_register(
          data.client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (!data.port)
    {
      error<driver_error>("midi_in_jack::open_virtual_port: JACK error creating virtual port");
    }
  }

  void close_port() override
  {
    if (data.port == nullptr)
      return;
    jack_port_unregister(data.client, data.port);
    data.port = nullptr;

    connected_ = false;
  }

  void set_client_name(std::string_view clientName) override
  {
    warning(
        "midi_in_jack::setClientName: this function is not implemented for the "
        "UNIX_JACK API!");
  }

  void set_port_name(std::string_view portName) override
  {
#if defined(LIBREMIDI_JACK_HAS_PORT_RENAME)
    jack_port_rename(data.client, data.port, portName.data());
#else
    jack_port_set_name(data.port, portName.data());
#endif
  }

  unsigned int get_port_count() const override
  {
    int count = 0;
    if (!data.client)
      return 0;

    // List of available ports
    auto ports = jack_get_ports(data.client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);

    if (!ports)
      return 0;

    while (ports[count] != nullptr)
      count++;

    jack_free(ports);

    return count;
  }

  std::string get_port_name(unsigned int portNumber) const override
  {

    unique_handle<const char*, jack_free> ports{
        jack_get_ports(data.client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput)};

    return jack_helpers::get_port_name(*this, ports.get(), portNumber);
  }

private:
  void connect()
  {
    if (data.client)
      return;

    // Initialize JACK client
    data.client = jack_client_open(clientName.c_str(), JackNoStartServer, nullptr);
    if (data.client == nullptr)
    {
      warning("midi_in_jack::initialize: JACK server not running?");
      return;
    }

    jack_set_process_callback(data.client, jackProcessIn, &this->inputData_);
    jack_activate(data.client);
  }

  static int jackProcessIn(jack_nframes_t nframes, void* arg)
  {
    midi_in_api::in_data& rtData = *(midi_in_api::in_data*)arg;
    auto& jData = *(jack_in_data*)rtData.apiData;
    jack_midi_event_t event;
    jack_time_t time;

    // Is port created?
    if (jData.port == nullptr)
      return 0;
    void* buff = jack_port_get_buffer(jData.port, nframes);

    // We have midi events in buffer
    uint32_t evCount = jack_midi_get_event_count(buff);
    for (uint32_t j = 0; j < evCount; j++)
    {
      message& m = rtData.message;

      jack_midi_event_get(&event, buff, j);

      // Compute the delta time.
      time = jack_get_time();
      if (rtData.firstMessage == true)
      {
        rtData.firstMessage = false;
        m.timestamp = 0;
      }
      else
      {
        m.timestamp = (time - jData.lastTime) * 0.000001;
      }

      jData.lastTime = time;
      if (!rtData.continueSysex)
        m.clear();

      if (!((rtData.continueSysex || event.buffer[0] == 0xF0) && (rtData.ignoreFlags & 0x01)))
      {
        // Unless this is a (possibly continued) SysEx message and we're ignoring SysEx,
        // copy the event buffer into the MIDI message struct.
        m.bytes.insert(m.bytes.end(), event.buffer, event.buffer + event.size);
      }

      switch (event.buffer[0])
      {
        case 0xF0:
          // Start of a SysEx message
          rtData.continueSysex = event.buffer[event.size - 1] != 0xF7;
          if (rtData.ignoreFlags & 0x01)
            continue;
          break;
        case 0xF1:
        case 0xF8:
          // MIDI Time Code or Timing Clock message
          if (rtData.ignoreFlags & 0x02)
            continue;
          break;
        case 0xFE:
          // Active Sensing message
          if (rtData.ignoreFlags & 0x04)
            continue;
          break;
        default:
          if (rtData.continueSysex)
          {
            // Continuation of a SysEx message
            rtData.continueSysex = event.buffer[event.size - 1] != 0xF7;
            if (rtData.ignoreFlags & 0x01)
              continue;
          }
          // All other MIDI messages
      }

      if (!rtData.continueSysex)
      {
        // If not a continuation of a SysEx message,
        // invoke the user callback function or queue the message.
        rtData.on_message_received(std::move(m));
      }
    }

    return 0;
  }

  std::string clientName;
  jack_in_data data;
};
}
