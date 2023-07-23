#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/backends/jack/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

#include <chrono>

namespace libremidi
{
class midi_in_jack final
    : public midi_in_api
    , private jack_helpers
    , public error_handler
{
public:
  struct
      : input_configuration
      , jack_input_configuration
  {
  } configuration;

  explicit midi_in_jack(input_configuration&& conf, jack_input_configuration&& apiconf)
      : midi_in_api{}
      , configuration{std::move(conf), std::move(apiconf)}
  {
    // TODO do like the others
    this->port = nullptr;
    this->client = nullptr;

    connect();
  }

  ~midi_in_jack() override
  {
    midi_in_jack::close_port();

    if (this->client)
      jack_client_close(this->client);
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
    if (this->port == nullptr)
      this->port = jack_port_register(
          this->client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (this->port == nullptr)
    {
      error<driver_error>(configuration, "midi_in_jack::open_port: JACK error creating port");
      return;
    }

    // Connecting to the output
    std::string name = get_port_name(portNumber);
    jack_connect(this->client, name.c_str(), jack_port_name(this->port));

    connected_ = true;
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (!check_port_name_length(*this, configuration.client_name, portName))
      return;

    connect();
    if (!this->port)
      this->port = jack_port_register(
          this->client, portName.data(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (!this->port)
    {
      error<driver_error>(
          configuration, "midi_in_jack::open_virtual_port: JACK error creating virtual port");
    }
  }

  void close_port() override
  {
    if (this->port == nullptr)
      return;
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
    auto ports = jack_get_ports(this->client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);

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
        jack_get_ports(this->client, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput)};

    return jack_helpers::get_port_name(*this, ports.get(), portNumber);
  }

private:
  void connect()
  {
    if (this->client)
      return;

    // Initialize JACK client
    this->client
        = jack_client_open(this->configuration.client_name.c_str(), JackNoStartServer, nullptr);
    if (this->client == nullptr)
    {
      warning(configuration, "midi_in_jack::initialize: JACK server not running?");
      return;
    }

    jack_set_process_callback(this->client, jackProcessIn, this);
    jack_activate(this->client);
  }

  void set_timestamp(
      jack_nframes_t frame, jack_nframes_t start_frames, jack_time_t abs_usec,
      libremidi::message& msg) noexcept
  {
    switch (configuration.timestamps)
    {
      case input_configuration::NoTimestamp:
        msg.timestamp = 0;
        return;
      case input_configuration::Relative: {
        // FIXME continueSysex logic like in core_midi?
        // time_ns is roughly in CLOCK_MONOTONIC time frame (at least on linux)
        const auto time_ns = 1000 * jack_frames_to_time(client, frame + start_frames);
        if (firstMessage == true)
        {
          firstMessage = false;
          msg.timestamp = 0;
        }
        else
        {
          msg.timestamp = time_ns - last_time;
        }

        last_time = time_ns;
        return;
      }
      case input_configuration::Absolute: {
        msg.timestamp = 1000 * jack_frames_to_time(client, frame + start_frames);
        break;
      }
      case input_configuration::SystemMonotonic: {
        namespace clk = std::chrono;
        msg.timestamp
            = clk::duration_cast<clk::nanoseconds>(clk::steady_clock::now().time_since_epoch())
                  .count();
        break;
      }
    }
  }

  static int jackProcessIn(jack_nframes_t nframes, void* arg)
  {
    auto& self = *(midi_in_jack*)arg;
    jack_midi_event_t event{};
    jack_time_t time{};

    // Is port created?
    if (self.port == nullptr)
      return 0;
    void* buff = jack_port_get_buffer(self.port, nframes);

    // Timing
    jack_nframes_t current_frames;
    jack_time_t current_usecs; // roughly CLOCK_MONOTONIC
    jack_time_t next_usecs;
    float period_usecs;
    jack_get_cycle_times(self.client, &current_frames, &current_usecs, &next_usecs, &period_usecs);

    // We have midi events in buffer
    uint32_t evCount = jack_midi_get_event_count(buff);
    for (uint32_t j = 0; j < evCount; j++)
    {
      auto& m = self.message;

      jack_midi_event_get(&event, buff, j);
      self.set_timestamp(event.time, current_frames, current_usecs, m);

      if (!self.continueSysex)
        m.clear();

      if (!((self.continueSysex || event.buffer[0] == 0xF0) && (self.configuration.ignore_sysex)))
      {
        // Unless this is a (possibly continued) SysEx message and we're ignoring SysEx,
        // copy the event buffer into the MIDI message struct.
        m.bytes.insert(m.bytes.end(), event.buffer, event.buffer + event.size);
      }

      switch (event.buffer[0])
      {
        case 0xF0:
          // Start of a SysEx message
          self.continueSysex = event.buffer[event.size - 1] != 0xF7;
          if (self.configuration.ignore_sysex)
            continue;
          break;
        case 0xF1:
        case 0xF8:
          // MIDI Time Code or Timing Clock message
          if (self.configuration.ignore_timing)
            continue;
          break;
        case 0xFE:
          // Active Sensing message
          if (self.configuration.ignore_sensing)
            continue;
          break;
        default:
          if (self.continueSysex)
          {
            // Continuation of a SysEx message
            self.continueSysex = event.buffer[event.size - 1] != 0xF7;
            if (self.configuration.ignore_sysex)
              continue;
          }
          // All other MIDI messages
      }

      if (!self.continueSysex)
      {
        // If not a continuation of a SysEx message,
        // invoke the user callback function or queue the message.
        self.configuration.on_message(std::move(m));
        m.clear();
      }
    }

    return 0;
  }

  jack_client_t* client{};
  jack_port_t* port{};
  jack_time_t last_time{};
};
}
