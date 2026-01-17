#pragma once
#if 0
  #include "libremidi/detail/midi_stream_decoder.hpp"

  #include <libremidi/backends/alsa_seq/helpers.hpp>
  #include <libremidi/backends/alsa_seq_ump/endpoint_config.hpp>
  #include <libremidi/backends/linux/helpers.hpp>
  #include <libremidi/detail/ump_endpoint_api.hpp>
  #include <libremidi/error_handler.hpp>

  #include <alsa/asoundlib.h>
  #if LIBREMIDI_ALSA_HAS_UMP
    #include <alsa/ump.h>
  #endif

  #include <atomic>
  #include <thread>

namespace libremidi::alsa_seq_ump
{

/// Base implementation for ALSA Seq UMP bidirectional endpoints
class endpoint_impl_base
    : public ump_endpoint_api
    , public alsa_seq::alsa_data
    , public error_handler
{
public:
  struct
      : libremidi::remote_ump_endpoint_configuration
      , alsa_seq_ump::endpoint_api_configuration
  {
  } configuration;

  explicit endpoint_impl_base(
      libremidi::remote_ump_endpoint_configuration&& conf,
      alsa_seq_ump::endpoint_api_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (!snd.seq.available || !snd.seq.ump.available)
    {
      client_open_ = std::errc::function_not_supported;
      return;
    }

    // Initialize the sequencer client
    if (init_seq_client() < 0)
    {
      client_open_ = std::errc::io_error;
      return;
    }
  #else
    client_open_ = std::errc::function_not_supported;
  #endif
  }

  ~endpoint_impl_base() override = default;

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::ALSA_SEQ_UMP;
  }

  stdx::error close() override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    unsubscribe();

    if (vport >= 0)
    {
      snd.seq.delete_port(seq, vport);
      vport = -1;
    }

    if (seq && !configuration.context)
    {
      snd.seq.close(seq);
      seq = nullptr;
    }

    port_open_ = false;
    connected_ = false;
    connected_endpoint_.reset();
  #endif
    return stdx::error{};
  }

  stdx::error send_ump(const uint32_t* words, size_t count) override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (!seq || vport < 0)
    {
      libremidi_handle_error(this->configuration, "trying to send a message without an open port.");
      return std::errc::not_connected;
    }

    snd_seq_ump_event_t ev;
    memset(&ev, 0, sizeof(snd_seq_ump_event_t));
    snd_seq_ev_set_ump(&ev);
    snd_seq_ev_set_source(&ev, vport);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    // Send each UMP packet
    size_t offset = 0;
    while (offset < count)
    {
      // Determine message size from first word
      uint8_t type = (words[offset] >> 28) & 0x0F;
      size_t msg_words = 1;
      switch (type)
      {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x6:
        case 0x7:
          msg_words = 1;
          break;
        case 0x3:
        case 0x4:
        case 0x8:
        case 0x9:
        case 0xA:
          msg_words = 2;
          break;
        case 0xB:
        case 0xC:
          msg_words = 3;
          break;
        case 0x5:
        case 0xD:
        case 0xE:
        case 0xF:
          msg_words = 4;
          break;
      }

      if (offset + msg_words > count)
        break;

      std::memcpy(ev.ump, words + offset, msg_words * sizeof(uint32_t));

      int ret = snd.seq.ump.event_output_direct(seq, &ev);
      if (ret < 0)
      {
        libremidi_handle_warning(this->configuration, "error sending MIDI message.");
        return from_errc(ret);
      }

      offset += msg_words;
    }

    snd.seq.drain_output(seq);
    return stdx::error{};
  #else
    return std::errc::function_not_supported;
  #endif
  }

  int64_t current_time() const noexcept override { return system_ns(); }

protected:
  #if LIBREMIDI_ALSA_HAS_UMP
  int init_seq_client()
  {
    if (configuration.context)
    {
      seq = configuration.context;
      return 0;
    }

    int ret = snd.seq.open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (ret < 0)
      return ret;

    // FIXME if (!configuration.client_name.empty())
    // FIXME   snd.seq.set_client_name(seq, configuration.client_name.c_str());

    // Set to MIDI 2.0 mode
    if (snd.seq.set_client_midi_version)
      snd.seq.set_client_midi_version(seq, SND_SEQ_CLIENT_UMP_MIDI_2_0);

    return 0;
  }

  [[nodiscard]] stdx::error
  do_open(const ump_endpoint_info& endpoint, std::string_view local_name)
  {
    // Parse endpoint ID to get client:port
    const std::string* id_str = std::get_if<std::string>(&endpoint.endpoint_id);
    if (!id_str)
      return std::errc::invalid_argument;

    int target_client = 0;
    int target_port = 0;

    auto colon_pos = id_str->find(':');
    if (colon_pos != std::string::npos)
    {
      target_client = std::stoi(id_str->substr(0, colon_pos));
      target_port = std::stoi(id_str->substr(colon_pos + 1));
    }
    else
    {
      target_client = std::stoi(*id_str);
      target_port = 0;
    }

    // Create our port with both read and write capabilities
    constexpr unsigned int caps = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE
                                  | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE
                                  | SND_SEQ_PORT_CAP_UMP_ENDPOINT;

    if (int ret
        = alsa_data::create_port(*this, local_name, caps, SND_SEQ_PORT_TYPE_MIDI_GENERIC, {});
        ret < 0)
    {
      libremidi_handle_error(configuration, "ALSA error creating port.");
      return from_errc(ret);
    }

    // Connect for receiving (target -> us)
    bool need_input = endpoint.has_input() && (configuration.on_message || configuration.on_raw_data);
    bool need_output = endpoint.has_output();

    if (need_input)
    {
      snd_seq_addr_t sender{};
      sender.client = static_cast<unsigned char>(target_client);
      sender.port = static_cast<unsigned char>(target_port);

      snd_seq_addr_t receiver{};
      receiver.client = static_cast<unsigned char>(snd.seq.client_id(seq));
      receiver.port = static_cast<unsigned char>(vport);

      // Subscribe to receive from target
      snd_seq_port_subscribe_t* sub{};
      snd_seq_port_subscribe_alloca(&sub);
      snd.seq.port_subscribe_set_sender(sub, &sender);
      snd.seq.port_subscribe_set_dest(sub, &receiver);
      snd.seq.port_subscribe_set_time_update(sub, 1);
      snd.seq.port_subscribe_set_time_real(sub, 1);

      if (int err = snd.seq.subscribe_port(seq, sub); err < 0)
      {
        libremidi_handle_warning(configuration, "Could not subscribe for input.");
      }
    }

    if (need_output)
    {
      snd_seq_addr_t sender{};
      sender.client = static_cast<unsigned char>(snd.seq.client_id(seq));
      sender.port = static_cast<unsigned char>(vport);

      snd_seq_addr_t receiver{};
      receiver.client = static_cast<unsigned char>(target_client);
      receiver.port = static_cast<unsigned char>(target_port);

      // Subscribe to send to target
      snd_seq_port_subscribe_t* sub{};
      snd_seq_port_subscribe_alloca(&sub);
      snd.seq.port_subscribe_set_sender(sub, &sender);
      snd.seq.port_subscribe_set_dest(sub, &receiver);

      if (int err = snd.seq.subscribe_port(seq, sub); err < 0)
      {
        libremidi_handle_warning(configuration, "Could not subscribe for output.");
      }
    }

    connected_endpoint_ = endpoint;
    port_open_ = true;
    connected_ = true;

    return stdx::error{};
  }

  int process_ump_event(const snd_seq_ump_event_t& ev)
  {
    // Filter the message types
    switch (ev.type)
    {
      case SND_SEQ_EVENT_PORT_SUBSCRIBED:
      case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
        return 0;

      case SND_SEQ_EVENT_QFRAME:
      case SND_SEQ_EVENT_TICK:
      case SND_SEQ_EVENT_CLOCK:
        if (configuration.ignore_timing)
          return 0;
        break;

      case SND_SEQ_EVENT_SENSING:
        if (configuration.ignore_sensing)
          return 0;
        break;

      case SND_SEQ_EVENT_SYSEX:
        if (configuration.ignore_sysex)
          return 0;
        break;
    }

    const int64_t timestamp = static_cast<int64_t>(ev.time.time.tv_sec) * 1'000'000'000
                              + static_cast<int64_t>(ev.time.time.tv_nsec);

    dispatch_ump(ev.ump, 4, timestamp);
    return 0;
  }

  int process_ump_events()
  {
    snd_seq_ump_event_t* ev{};
    alsa_seq::event_handle handle{snd};
    int result = 0;
    while ((result = snd.seq.ump.event_input(seq, &ev)) > 0)
    {
      handle.reset((snd_seq_event_t*)ev);
      if (int err = process_ump_event(*ev); err < 0)
        return err;
    }
    return result;
  }

  void dispatch_ump(const uint32_t* words, size_t max_words, int64_t timestamp)
  {
    // Call raw callback if set
    if (configuration.on_raw_data)
    {
      configuration.on_raw_data({words, max_words}, timestamp);
    }

    // Call message callback for each complete UMP
    if (configuration.on_message)
    {
      size_t offset = 0;
      while (offset < max_words)
      {
        ump msg;
        msg.data[0] = words[offset];

        // Determine message size from type
        uint8_t type = (msg.data[0] >> 28) & 0x0F;
        size_t msg_words = 1;
        switch (type)
        {
          case 0x0:
          case 0x1:
          case 0x2:
          case 0x6:
          case 0x7:
            msg_words = 1;
            break;
          case 0x3:
          case 0x4:
          case 0x8:
          case 0x9:
          case 0xA:
            msg_words = 2;
            break;
          case 0xB:
          case 0xC:
            msg_words = 3;
            break;
          case 0x5:
          case 0xD:
          case 0xE:
          case 0xF:
            msg_words = 4;
            break;
        }

        if (offset + msg_words > max_words)
          break;

        for (size_t i = 1; i < msg_words && i < 4; ++i)
          msg.data[i] = words[offset + i];

        msg.timestamp = timestamp;
        configuration.on_message(std::move(msg));

        offset += msg_words;
      }
    }
  }
  #endif
};

/// Threaded implementation with internal polling
class endpoint_impl_threaded final : public endpoint_impl_base
{
public:
  endpoint_impl_threaded(
      libremidi::remote_ump_endpoint_configuration&& conf,
      alsa_seq_ump::endpoint_api_configuration&& apiconf)
      : endpoint_impl_base{std::move(conf), std::move(apiconf)}
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (m_termination_event < 0)
    {
      libremidi_handle_error(this->configuration, "error creating eventfd.");
      return;
    }
    client_open_ = stdx::error{};
  #endif
  }

  ~endpoint_impl_threaded() override
  {
    close();
    client_open_ = std::errc::not_connected;
  }

  stdx::error open(const ump_endpoint_info& endpoint, std::string_view local_name) override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (auto err = do_open(endpoint, local_name); err.is_set())
      return err;

    // Start reading thread if we have input capability and callbacks
    if (configuration.on_message || configuration.on_raw_data)
    {
      if (auto err = start_thread(); err.is_set())
        return err;
    }

    return stdx::error{};
  #else
    return std::errc::function_not_supported;
  #endif
  }

  stdx::error close() override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    m_termination_event.notify();
    if (m_thread.joinable())
      m_thread.join();
    m_termination_event.consume();
  #endif
    return endpoint_impl_base::close();
  }

private:
  #if LIBREMIDI_ALSA_HAS_UMP
  [[nodiscard]] stdx::error start_thread()
  {
    try
    {
      m_thread = std::thread{[this] { run_thread(); }};
      return stdx::error{};
    }
    catch (const std::system_error& e)
    {
      libremidi_handle_error(
          this->configuration, "error starting MIDI input thread: " + std::string(e.what()));
      return e.code();
    }
  }

  void run_thread()
  {
    int poll_fd_count = snd.seq.poll_descriptors_count(seq, POLLIN) + 1;
    auto poll_fds = (struct pollfd*)alloca(poll_fd_count * sizeof(struct pollfd));
    poll_fds[0] = m_termination_event;
    snd.seq.poll_descriptors(seq, poll_fds + 1, poll_fd_count - 1, POLLIN);

    const auto period
        = std::chrono::duration_cast<std::chrono::milliseconds>(configuration.poll_period).count();

    for (;;)
    {
      if (snd.seq.event_input_pending(seq, 1) == 0)
      {
        if (poll(poll_fds, poll_fd_count, static_cast<int32_t>(period)) >= 0)
        {
          if (m_termination_event.ready(poll_fds[0]))
            break;
        }
        continue;
      }

      int64_t res = process_ump_events();
      if (res < 0)
        LIBREMIDI_LOG("MIDI input error: ", snd.strerror(res));
    }
  }

  std::thread m_thread;
  eventfd_notifier m_termination_event{};
  #endif
};

/// Manual polling implementation for external event loop integration
class endpoint_impl_manual final : public endpoint_impl_base
{
public:
  endpoint_impl_manual(
      libremidi::remote_ump_endpoint_configuration&& conf,
      alsa_seq_ump::endpoint_api_configuration&& apiconf)
      : endpoint_impl_base{std::move(conf), std::move(apiconf)}
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    client_open_ = stdx::error{};
  #endif
  }

  ~endpoint_impl_manual() override
  {
    close();
    client_open_ = std::errc::not_connected;
  }

  stdx::error open(const ump_endpoint_info& endpoint, std::string_view local_name) override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (auto err = do_open(endpoint, local_name); err.is_set())
      return err;

    // Set up manual polling callback
    if (configuration.manual_poll)
    {
      configuration.manual_poll(
          alsa_seq::poll_parameters{.addr = vaddr, .callback = [this](const auto& ev) -> int {
        return process_ump_event(*reinterpret_cast<const snd_seq_ump_event*>(&ev));
      }});
    }

    return stdx::error{};
  #else
    return std::errc::function_not_supported;
  #endif
  }

  stdx::error close() override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (configuration.stop_poll)
      configuration.stop_poll(vaddr);
  #endif
    return endpoint_impl_base::close();
  }
};

}
#endif
