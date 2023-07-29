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
    : public midi1::out_api
    , public jack_helpers
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
      , queue{configuration.ringbuffer_size}
  {
    auto status = connect(*this);
    if (status != jack_status_t{})
      warning(configuration, "midi_in_jack: " + std::to_string((int)jack_status_t{}));
  }

  ~midi_out_jack() override
  {
    midi_out_jack::close_port();

    disconnect(*this);

    if (this->client && !configuration.context)
      jack_client_close(this->client);
  }

  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_out_jack: set_client_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::JACK_MIDI; }

  bool open_port(const output_port& port, std::string_view portName) override
  {
    if (!create_local_port(*this, portName, JackPortIsOutput))
      return false;

    // Connecting to the output
    if (jack_connect(this->client, jack_port_name(this->port), port.port_name.c_str()) != 0)
    {
      error<invalid_parameter_error>(
          configuration, "JACK: could not connect to port" + port.port_name);
      return false;
    }

    return true;
  }

  bool open_virtual_port(std::string_view portName) override
  {
    return create_local_port(*this, portName, JackPortIsOutput);
  }

  void close_port() override { return do_close_port(); }

  void set_port_name(std::string_view portName) override
  {
    jack_port_rename(this->client, this->port, portName.data());
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    queue.write(message, size);
  }

  int process(jack_nframes_t nframes)
  {
    void* buff = jack_port_get_buffer(this->port, nframes);
    jack_midi_clear_buffer(buff);

    this->queue.read(buff);

    return 0;
  }

private:
  jack_queue queue;
};

}
