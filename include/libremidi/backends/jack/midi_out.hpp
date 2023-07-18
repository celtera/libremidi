#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{

class midi_out_jack final
    : public midi_out_api
    , private jack_helpers
{
public:
  midi_out_jack(std::string_view cname)
  {
    data.port = nullptr;
    data.client = nullptr;
    this->clientName = cname;

    connect();
  }

  ~midi_out_jack() override
  {
    midi_out_jack::close_port();

    // Cleanup
    jack_ringbuffer_free(data.buffSize);
    jack_ringbuffer_free(data.buffMessage);
    if (data.client)
    {
      jack_client_close(data.client);
    }
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::UNIX_JACK; }

  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (!check_port_name_length(*this, clientName, portName))
      return;

    connect();

    // Creating new port
    if (!data.port)
      data.port = jack_port_register(
          data.client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

    if (!data.port)
    {
      error<driver_error>("midi_out_jack::open_port: JACK error creating port");
      return;
    }

    // Connecting to the output
    std::string name = get_port_name(portNumber);
    jack_connect(data.client, jack_port_name(data.port), name.c_str());

    connected_ = true;
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (!check_port_name_length(*this, clientName, portName))
      return;

    connect();
    if (data.port == nullptr)
      data.port = jack_port_register(
          data.client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

    if (data.port == nullptr)
    {
      error<driver_error>("midi_out_jack::open_virtual_port: JACK error creating virtual port");
    }
  }

  void close_port() override
  {
    using namespace std::literals;
    if (data.port == nullptr)
      return;

    data.sem_needpost.notify();
    data.sem_cleanup.wait_for(1s);

    jack_port_unregister(data.client, data.port);
    data.port = nullptr;

    connected_ = false;
  }

  void set_client_name(std::string_view clientName) override
  {
    warning(
        "midi_out_jack::setClientName: this function is not implemented for the "
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
    const char** ports
        = jack_get_ports(data.client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);

    if (ports == nullptr)
      return 0;
    while (ports[count] != nullptr)
      count++;

    jack_free(ports);

    return count;
  }

  std::string get_port_name(unsigned int portNumber) const override
  {
    // List of available ports
    unique_handle<const char*, jack_free> ports{
        jack_get_ports(data.client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput)};

    return jack_helpers::get_port_name(*this, ports.get(), portNumber);
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    int nBytes = static_cast<int>(size);

    // Write full message to buffer
    jack_ringbuffer_write(data.buffMessage, (const char*)message, nBytes);
    jack_ringbuffer_write(data.buffSize, (char*)&nBytes, sizeof(nBytes));
  }

private:
  std::string clientName;

  void connect()
  {
    if (data.client)
      return;

    // Initialize output ringbuffers
    data.buffSize = jack_ringbuffer_create(jack_out_data::ringbuffer_size);
    data.buffMessage = jack_ringbuffer_create(jack_out_data::ringbuffer_size);

    // Initialize JACK client
    data.client = jack_client_open(clientName.c_str(), JackNoStartServer, nullptr);
    if (data.client == nullptr)
    {
      warning("midi_out_jack::initialize: JACK server not running?");
      return;
    }

    jack_set_process_callback(data.client, jackProcessOut, &data);
    jack_activate(data.client);
  }

  static int jackProcessOut(jack_nframes_t nframes, void* arg)
  {
    auto& data = *(jack_out_data*)arg;

    // Is port created?
    if (data.port == nullptr)
      return 0;

    void* buff = jack_port_get_buffer(data.port, nframes);
    jack_midi_clear_buffer(buff);

    while (jack_ringbuffer_read_space(data.buffSize) > 0)
    {
      int space{};
      jack_ringbuffer_read(data.buffSize, (char*)&space, sizeof(int));
      auto midiData = jack_midi_event_reserve(buff, 0, space);

      jack_ringbuffer_read(data.buffMessage, (char*)midiData, space);
    }

    if (!data.sem_needpost.try_wait())
      data.sem_cleanup.notify();

    return 0;
  }

  jack_out_data data;
};

}
