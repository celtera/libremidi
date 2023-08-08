#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/backends/jack/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

#include <chrono>

namespace libremidi
{
class midi_in_jack final
    : public midi1::in_api
    , public jack_helpers
    , public error_handler
{
public:
  struct
      : input_configuration
      , jack_input_configuration
  {
  } configuration;

  explicit midi_in_jack(input_configuration&& conf, jack_input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    auto status = connect(*this);
    if (status != jack_status_t{})
      warning(configuration, "midi_in_jack: " + std::to_string((int)jack_status_t{}));
  }

  ~midi_in_jack() override
  {
    midi_in_jack::close_port();

    disconnect(*this);

    if (this->client && !configuration.context)
      jack_client_close(this->client);
  }

  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_jack: set_client_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::JACK_MIDI; }

  bool open_port(const input_port& port, std::string_view portName) override
  {
    if (!create_local_port(*this, portName, JackPortIsInput))
      return false;

    if (auto ret = jack_connect(this->client, port.port_name.c_str(), jack_port_name(this->port));
        ret != 0)
    {
      error<invalid_parameter_error>(
          configuration, "JACK: could not connect to port: " + port.port_name);
      return false;
    }
    return true;
  }

  bool open_virtual_port(std::string_view portName) override
  {
    return create_local_port(*this, portName, JackPortIsInput);
  }

  void close_port() override { return do_close_port(); }

  void set_port_name(std::string_view portName) override
  {
    jack_port_rename(this->client, this->port, portName.data());
  }

  void set_timestamp(
      jack_nframes_t frame, jack_nframes_t start_frames, jack_time_t abs_usec,
      libremidi::message& msg) noexcept
  {
    switch (configuration.timestamps)
    {
      case timestamp_mode::NoTimestamp:
        msg.timestamp = 0;
        return;
      case timestamp_mode::Relative: {
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
      case timestamp_mode::Absolute: {
        msg.timestamp = 1000 * jack_frames_to_time(client, frame + start_frames);
        break;
      }
      case timestamp_mode::SystemMonotonic: {
        namespace clk = std::chrono;
        msg.timestamp
            = clk::duration_cast<clk::nanoseconds>(clk::steady_clock::now().time_since_epoch())
                  .count();
        break;
      }
      case timestamp_mode::AudioFrame:
        msg.timestamp = frame;
        break;
    }
  }

  int process(jack_nframes_t nframes)
  {
    void* buff = jack_port_get_buffer(this->port, nframes);

    // Timing
    jack_nframes_t current_frames;
    jack_time_t current_usecs; // roughly CLOCK_MONOTONIC
    jack_time_t next_usecs;
    float period_usecs;
    jack_get_cycle_times(
        this->client, &current_frames, &current_usecs, &next_usecs, &period_usecs);

    // We have midi events in buffer
    uint32_t evCount = jack_midi_get_event_count(buff);
    for (uint32_t j = 0; j < evCount; j++)
    {
      auto& m = this->message;

      jack_midi_event_t event{};
      jack_midi_event_get(&event, buff, j);
      this->set_timestamp(event.time, current_frames, current_usecs, m);

      if (!this->continueSysex)
        m.clear();

      if (!((this->continueSysex || event.buffer[0] == 0xF0)
            && (this->configuration.ignore_sysex)))
      {
        // Unless this is a (possibly continued) SysEx message and we're ignoring SysEx,
        // copy the event buffer into the MIDI message struct.
        m.bytes.insert(m.bytes.end(), event.buffer, event.buffer + event.size);
      }

      switch (event.buffer[0])
      {
        case 0xF0:
          // Start of a SysEx message
          this->continueSysex = event.buffer[event.size - 1] != 0xF7;
          if (this->configuration.ignore_sysex)
            continue;
          break;
        case 0xF1:
        case 0xF8:
          // MIDI Time Code or Timing Clock message
          if (this->configuration.ignore_timing)
            continue;
          break;
        case 0xFE:
          // Active Sensing message
          if (this->configuration.ignore_sensing)
            continue;
          break;
        default:
          if (this->continueSysex)
          {
            // Continuation of a SysEx message
            this->continueSysex = event.buffer[event.size - 1] != 0xF7;
            if (this->configuration.ignore_sysex)
              continue;
          }
          // All other MIDI messages
      }

      if (!this->continueSysex)
      {
        // If not a continuation of a SysEx message,
        // invoke the user callback function or queue the message.
        this->configuration.on_message(std::move(m));
        m.clear();
      }
    }

    return 0;
  }

  jack_time_t last_time{};
};
}
