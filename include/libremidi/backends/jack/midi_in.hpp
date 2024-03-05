#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/backends/jack/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>

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
  }

  std::error_code set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_jack: set_client_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::JACK_MIDI; }

  std::error_code open_port(const input_port& port, std::string_view portName) override
  {
    if (!create_local_port(*this, portName, JackPortIsInput))
      return false;

    if (auto ret = jack_connect(this->client, port.port_name.c_str(), jack_port_name(this->port));
        ret != 0)
    {
      error<invalid_parameter_error>(
          configuration, "JACK: could not connect to port: " + port.port_name + " -> "
                             + jack_port_name(this->port));
      return false;
    }
    return true;
  }

  std::error_code open_virtual_port(std::string_view portName) override
  {
    return create_local_port(*this, portName, JackPortIsInput);
  }

  std::error_code close_port() override { return do_close_port(); }

  std::error_code set_port_name(std::string_view portName) override
  {
    jack_port_rename(this->client, this->port, portName.data());
  }

  timestamp absolute_timestamp() const noexcept override
  {
    return 1000 * jack_frames_to_time(client, jack_frame_time(client));
  }

  int process(jack_nframes_t nframes)
  {
    static constexpr timestamp_backend_info timestamp_info{
        .has_absolute_timestamps = true,
        .absolute_is_monotonic = true,
        .has_samples = true,
    };
    void* buff = jack_port_get_buffer(this->port, nframes);

    // Timing
    jack_nframes_t current_frames{};
    jack_time_t current_usecs{}; // roughly CLOCK_MONOTONIC
    jack_time_t next_usecs{};
    float period_usecs{};
    jack_get_cycle_times(
        this->client, &current_frames, &current_usecs, &next_usecs, &period_usecs);

    // We have midi events in buffer
    uint32_t evCount = jack_midi_get_event_count(buff);
    for (uint32_t j = 0; j < evCount; j++)
    {
      jack_midi_event_t event{};
      jack_midi_event_get(&event, buff, j);
      const auto to_ns
          = [=, this] { return 1000 * jack_frames_to_time(client, current_frames + event.time); };

      m_processing.on_bytes(
          {event.buffer, event.buffer + event.size},
          m_processing.timestamp<timestamp_info>(to_ns, event.time));
    }

    return 0;
  }

  midi1::input_state_machine m_processing{this->configuration};
};
}
