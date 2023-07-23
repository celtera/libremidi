#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/backends/jack/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

#include <semaphore>

namespace libremidi
{
struct jack_queue
{
public:
  static constexpr auto size_sz = sizeof(int32_t);

  jack_queue() = default;
  jack_queue(const jack_queue&) = delete;
  jack_queue(jack_queue&&) = delete;
  jack_queue& operator=(const jack_queue&) = delete;

  jack_queue& operator=(jack_queue&& other) noexcept
  {
    ringbuffer = other.ringbuffer;
    ringbuffer_space = other.ringbuffer_space;
    other.ringbuffer = nullptr;
    return *this;
  }

  explicit jack_queue(int sz) noexcept
  {
    ringbuffer = jack_ringbuffer_create(sz);
    ringbuffer_space = (int)jack_ringbuffer_write_space(ringbuffer);
  }

  ~jack_queue() noexcept
  {
    if (ringbuffer)
      jack_ringbuffer_free(ringbuffer);
  }

  void write(const unsigned char* data, int32_t sz) noexcept
  {
    if (sz + size_sz > ringbuffer_space)
      return;

    while (jack_ringbuffer_write_space(ringbuffer) < size_sz + sz)
      sched_yield();

    jack_ringbuffer_write(ringbuffer, (char*)&sz, size_sz);
    jack_ringbuffer_write(ringbuffer, (const char*)data, sz);
  }

  void read(void* jack_events) noexcept
  {
    jack_midi_clear_buffer(jack_events);

    int32_t sz;
    while (jack_ringbuffer_peek(ringbuffer, (char*)&sz, size_sz) == size_sz
           && jack_ringbuffer_read_space(ringbuffer) >= size_sz + sz)
    {
      jack_ringbuffer_read_advance(ringbuffer, size_sz);

      if (auto midi = jack_midi_event_reserve(jack_events, 0, sz))
        jack_ringbuffer_read(ringbuffer, (char*)midi, sz);
      else
        jack_ringbuffer_read_advance(ringbuffer, sz);
    }
  }

  jack_ringbuffer_t* ringbuffer{};
  int32_t ringbuffer_space{}; // actual writable size, usually 1 less than ringbuffer
};

class midi_out_jack final
    : public midi_out_api
    , private jack_helpers
    , public error_handler
{
public:
  struct
      : output_configuration
      , jack_output_configuration
  {
  } configuration;

  midi_out_jack(output_configuration&& conf, jack_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    this->port = nullptr;
    this->client = nullptr;

    connect();
  }

  ~midi_out_jack() override
  {
    midi_out_jack::close_port();

    if (this->client)
    {
      jack_client_close(this->client);
    }
  }

  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_out_jack: set_client_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::UNIX_JACK; }

  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (!check_port_name_length(*this, configuration.client_name, portName))
      return;

    connect();

    // Creating new port
    if (!this->port)
      this->port = jack_port_register(
          this->client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

    if (!this->port)
    {
      error<driver_error>(configuration, "midi_out_jack::open_port: JACK error creating port");
      return;
    }

    // Connecting to the output
    std::string name = get_port_name(portNumber);
    jack_connect(this->client, jack_port_name(this->port), name.c_str());

    connected_ = true;
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (!check_port_name_length(*this, configuration.client_name, portName))
      return;

    connect();
    if (this->port == nullptr)
      this->port = jack_port_register(
          this->client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

    if (this->port == nullptr)
    {
      error<driver_error>(
          configuration, "midi_out_jack::open_virtual_port: JACK error creating virtual port");
    }
  }

  void close_port() override
  {
    using namespace std::literals;
    if (this->port == nullptr)
      return;

    this->sem_needpost.release();
    this->sem_cleanup.try_acquire_for(1s);

    jack_port_unregister(this->client, this->port);
    this->port = nullptr;

    connected_ = false;
  }

  void set_port_name(std::string_view portName) override
  {
#if defined(LIBREMIDI_JACK_HAS_PORT_RENAME)
    jack_port_rename(this->client, this->port, portName.data());
#else
    jack_port_set_name(this->port, portName.data());
#endif
  }

  unsigned int get_port_count() const override
  {
    int count = 0;
    if (!this->client)
      return 0;

    // List of available ports
    const char** ports
        = jack_get_ports(this->client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);

    if (ports == nullptr)
      return 0;
    while (ports[count] != nullptr)
      count++;

    jack_free(ports);

    return count;
  }

  std::string get_port_name(unsigned int portNumber) const override
  {
    unique_handle<const char*, jack_free> ports{
        jack_get_ports(this->client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput)};

    return jack_helpers::get_port_name(*this, ports.get(), portNumber);
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    queue.write(message, size);
  }

private:
  void connect()
  {
    if (this->client)
      return;

    // Initialize output ringbuffers
    this->queue = jack_queue{configuration.ringbuffer_size};

    // Initialize JACK client
    this->client = jack_client_open(configuration.client_name.c_str(), JackNoStartServer, nullptr);
    if (this->client == nullptr)
    {
      warning(configuration, "midi_out_jack::initialize: JACK server not running?");
      return;
    }

    jack_set_process_callback(this->client, jackProcessOut, this);
    jack_activate(this->client);
  }

  static int jackProcessOut(jack_nframes_t nframes, void* arg)
  {
    auto& self = *(midi_out_jack*)arg;

    // Is port created?
    if (self.port == nullptr)
      return 0;

    void* buff = jack_port_get_buffer(self.port, nframes);
    jack_midi_clear_buffer(buff);

    self.queue.read(buff);

    if (!self.sem_needpost.try_acquire())
      self.sem_cleanup.release();

    return 0;
  }

private:
  jack_client_t* client{};
  jack_port_t* port{};

  jack_queue queue;

  std::counting_semaphore<> sem_cleanup{0};
  std::counting_semaphore<> sem_needpost{0};
};

}
