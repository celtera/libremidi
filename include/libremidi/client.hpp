#pragma once
#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>
#include <libremidi/shared_context.hpp>

#include <map>

namespace libremidi::midi1
{
struct client_configuration
{
  libremidi::API api = libremidi::midi1::default_api();

  //! A client name, if the backend allows it (e.g. JACK, ALSA SEQ...)
  std::string_view client_name = "libremidi client";

  //! Set a callback function to be invoked for incoming MIDI messages.
  //! Mandatory!
  std::function<void(const libremidi::input_port&, message&&)> on_message
      = [](const libremidi::input_port& /*port*/, libremidi::message&&) {};

  //! Observation callbacks for when ports are added or removed
  input_port_callback input_added;
  input_port_callback input_removed;
  output_port_callback output_added;
  output_port_callback output_removed;

  //! Error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  midi_error_callback on_error{};
  midi_error_callback on_warning{};

  //! Poll period for observation polling operations, if relevant to the backend
  std::chrono::milliseconds poll_period{100};

  //! Specify whether certain MIDI message types should be queued or ignored
  //! during input.
  /*!
    By default, MIDI timing and active sensing messages are ignored
    during message input because of their relative high data rates.
    MIDI sysex messages are ignored by default as well.  Variable
    values of "true" imply that the respective message type will be
    ignored.
  */
  uint32_t ignore_sysex : 1 = true;
  uint32_t ignore_timing : 1 = true;
  uint32_t ignore_sensing : 1 = true;
  uint32_t timestamps : 3 = timestamp_mode::Absolute;

  //! Observe hardware ports
  uint32_t track_hardware : 1 = true;

  //! Observe software (virtual) ports if the API provides it
  uint32_t track_virtual : 1 = false;
};

class client
{
public:
  explicit client(const client_configuration& conf)
      : client{conf, create_shared_context(conf.api, conf.client_name)}
  {
  }

  explicit client(const client_configuration& conf, shared_configurations ctx)
      : configuration{conf}
      , context{ctx}
      , m_observer{
            observer_configuration{
                .on_error = conf.on_error,
                .on_warning = conf.on_warning,

                .input_added = conf.input_added,
                .input_removed = conf.input_removed,
                .output_added = conf.output_added,
                .output_removed = conf.output_removed,

                .track_hardware = conf.track_hardware,
                .track_virtual = conf.track_virtual,
                .notify_in_constructor = false},
            context.observer}
  {
    if (context.context)
      context.context->start_processing();
  }

  ~client()
  {
    m_inputs.clear();
    m_outputs.clear();

    if (context.context)
      context.context->stop_processing();
  }

  std::vector<libremidi::input_port> get_input_ports() const noexcept
  {
    return m_observer.get_input_ports();
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept
  {
    return m_observer.get_output_ports();
  }

  void add_input(const input_port& port, std::string_view name)
  {
    if (m_inputs.find(port) != m_inputs.end())
      return;

    auto res = m_inputs.try_emplace(
        port,
        input_configuration{
            .on_message
            = [this, port](libremidi::message&& m) {
      configuration.on_message(port, std::move(m));
            },

            .on_error = configuration.on_error,
            .on_warning = configuration.on_warning,

            .ignore_sysex = configuration.ignore_sysex,
            .ignore_timing = configuration.ignore_timing,
            .ignore_sensing = configuration.ignore_sensing,

            .timestamps = configuration.timestamps},
        context.in);

    res.first->second.open_port(port, name);
  }

  void add_output(const output_port& port, std::string_view name)
  {
    if (m_outputs.find(port) != m_outputs.end())
      return;

    auto res = m_outputs.try_emplace(
        port,
        output_configuration{
            .on_error = configuration.on_error,
            .on_warning = configuration.on_warning,

            .timestamps = configuration.timestamps},
        context.out);

    res.first->second.open_port(port, name);
  }

  void remove_input(const input_port& port) { m_inputs.erase(port); }
  void remove_output(const output_port& port) { m_outputs.erase(port); }

  void send_message(const unsigned char* message, size_t size)
  {
    for (auto& [_, out] : m_outputs)
    {
      out.send_message(message, size);
    }
  }

  void send_ump(const uint32_t* message, size_t size)
  {
    for (auto& [_, out] : m_outputs)
    {
      out.send_ump(message, size);
    }
  }

  void send_message(const output_port& port, const unsigned char* message, size_t size)
  {
    if (auto it = m_outputs.find(port); it != m_outputs.end())
      it->second.send_message(message, size);
  }

  void send_ump(const output_port& port, const uint32_t* message, size_t size)
  {
    if (auto it = m_outputs.find(port); it != m_outputs.end())
      it->second.send_ump(message, size);
  }

private:
  client_configuration configuration;
  shared_configurations context;

  std::map<input_port, midi_in> m_inputs;
  std::map<output_port, midi_out> m_outputs;

  observer m_observer;
};
}
#if defined(LIBREMIDI_HEADER_ONLY)
  #include "client.cpp"
#endif
