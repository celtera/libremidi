#pragma once
#if 0
  #include <libremidi/backends/alsa_raw_ump/endpoint_config.hpp>
  #include <libremidi/backends/linux/alsa.hpp>
  #include <libremidi/backends/linux/helpers.hpp>
  #include <libremidi/detail/midi_stream_decoder.hpp>
  #include <libremidi/detail/ump_endpoint_api.hpp>
  #include <libremidi/error_handler.hpp>

  #include <alsa/asoundlib.h>
  #if LIBREMIDI_ALSA_HAS_UMP
    #include <alsa/ump.h>
  #endif

  #include <atomic>
  #include <thread>

namespace libremidi::alsa_raw_ump
{

/// Base implementation for ALSA Raw UMP bidirectional endpoints
class endpoint_impl_base
    : public ump_endpoint_api
    , public error_handler
{
public:
  struct
      : libremidi::remote_ump_endpoint_configuration
      , alsa_raw_ump::endpoint_api_configuration
  {
  } configuration;

  const libasound& snd = libasound::instance();

  explicit endpoint_impl_base(
      libremidi::remote_ump_endpoint_configuration&& conf,
      alsa_raw_ump::endpoint_api_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (!snd.ump.available)
    {
      client_open_ = std::errc::function_not_supported;
      return;
    }
    fds_.reserve(4);
  #else
    client_open_ = std::errc::function_not_supported;
  #endif
  }

  ~endpoint_impl_base() override = default;

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::ALSA_RAW_UMP;
  }

  stdx::error close() override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (midiport_in_)
    {
      snd.ump.close(midiport_in_);
      midiport_in_ = nullptr;
    }
    if (midiport_out_ && midiport_out_ != midiport_in_)
    {
      snd.ump.close(midiport_out_);
    }
    midiport_out_ = nullptr;

    port_open_ = false;
    connected_ = false;
    connected_endpoint_.reset();
  #endif
    return stdx::error{};
  }

  stdx::error send_ump(const uint32_t* words, size_t count) override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (!midiport_out_)
    {
      libremidi_handle_error(
          this->configuration, "trying to send a message without an open port.");
      return std::errc::not_connected;
    }

    if (auto err = snd.ump.write(midiport_out_, words, count * sizeof(uint32_t)); err < 0)
    {
      libremidi_handle_error(this->configuration, "cannot write message.");
      return from_errc(err);
    }

    return stdx::error{};
  #else
    return std::errc::function_not_supported;
  #endif
  }

  int64_t current_time() const noexcept override { return system_ns(); }

  #if LIBREMIDI_ALSA_HAS_UMP
  /// Open the ALSA UMP device
  [[nodiscard]] stdx::error do_open(const char* device_name, bool need_input, bool need_output)
  {
    // Mode flags: SND_RAWMIDI_SYNC ensures writes are synchronous
    int mode = SND_RAWMIDI_SYNC;

    // Direction is determined by which pointers we pass (NULL = don't need that direction)
    snd_ump_t** in_ptr = need_input ? &midiport_in_ : nullptr;
    snd_ump_t** out_ptr = need_output ? &midiport_out_ : nullptr;

    int err = snd.ump.open(in_ptr, out_ptr, device_name, mode);
    if (err < 0)
    {
      libremidi_handle_error(
          this->configuration, "cannot open UMP device: " + std::string(snd.strerror(err)));
      return from_errc(err);
    }

    // Configure input if we have one
    if (midiport_in_)
    {
      if (auto config_err = configure_input(); config_err.is_set())
        return config_err;
    }

    port_open_ = true;
    connected_ = true;
    return stdx::error{};
  }

  [[nodiscard]] stdx::error configure_input()
  {
    snd_rawmidi_params_t* params{};
    snd_rawmidi_params_alloca(&params);

    auto rawmidi = snd.ump.rawmidi(midiport_in_);

    if (int err = snd.ump.rawmidi_params_current(midiport_in_, params); err < 0)
      return from_errc(err);

    if (int err = snd.rawmidi.params_set_no_active_sensing(rawmidi, params, 1); err < 0)
      return from_errc(err);

    // Configure timestamping based on configuration
    if (configuration.enable_jr_timestamps)
    {
      if (int err = snd.rawmidi.params_set_read_mode(rawmidi, params, SND_RAWMIDI_READ_TSTAMP);
          err < 0)
        return from_errc(err);
      if (int err = snd.rawmidi.params_set_clock_type(rawmidi, params, SND_RAWMIDI_CLOCK_MONOTONIC);
          err < 0)
        return from_errc(err);
      jr_timestamps_enabled_ = true;
    }
    else
    {
      if (int err = snd.rawmidi.params_set_read_mode(rawmidi, params, SND_RAWMIDI_READ_STANDARD);
          err < 0)
        return from_errc(err);
      if (int err = snd.rawmidi.params_set_clock_type(rawmidi, params, SND_RAWMIDI_CLOCK_NONE);
          err < 0)
        return from_errc(err);
    }

    if (int err = snd.ump.rawmidi_params(midiport_in_, params); err < 0)
      return from_errc(err);

    return init_pollfd();
  }

  [[nodiscard]] stdx::error init_pollfd()
  {
    int num_fds = snd.ump.poll_descriptors_count(midiport_in_);

    fds_.clear();
    fds_.resize(num_fds);

    int ret = snd.ump.poll_descriptors(midiport_in_, fds_.data(), num_fds);
    if (ret < 0)
      return from_errc(ret);
    return stdx::error{};
  }

  ssize_t do_read_events(auto parse_func, std::span<pollfd> fds)
  {
    if (fds.empty())
    {
      return (this->*parse_func)();
    }
    else
    {
      unsigned short res{};
      ssize_t err = snd.ump.poll_descriptors_revents(
          midiport_in_, fds.data(), static_cast<unsigned int>(fds.size()), &res);
      if (err < 0)
        return err;

      if (res & (POLLERR | POLLHUP))
      {
        // Connection lost
        if (configuration.on_disconnected)
          configuration.on_disconnected();
        return -EIO;
      }

      if (res & POLLIN)
        return (this->*parse_func)();
    }
    return 0;
  }

  ssize_t read_input_buffer()
  {
    static const constexpr int nwords = 64;
    uint32_t words[nwords];

    ssize_t err = 0;
    while ((err = snd.ump.read(midiport_in_, words, nwords * 4)) > 0)
    {
      const size_t word_count = err / 4;
      dispatch_messages(words, word_count, current_time());
    }
    return err;
  }

  ssize_t read_input_buffer_with_timestamps()
  {
    static const constexpr int nwords = 64;
    uint32_t words[nwords];
    struct timespec ts;

    ssize_t err = 0;
    while ((err = snd.ump.tread(midiport_in_, &ts, words, nwords * 4)) > 0)
    {
      const size_t word_count = err / 4;
      const int64_t timestamp
          = static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + static_cast<int64_t>(ts.tv_nsec);
      dispatch_messages(words, word_count, timestamp);
    }
    return err;
  }

  void dispatch_messages(const uint32_t* words, size_t word_count, int64_t timestamp)
  {
    // Apply input filter if configured
    if (configuration.input_filter.mode != group_filter_mode::all_groups)
    {
      // For now, we'd need to parse and filter each message
      // This is a simplified version that passes everything
    }

    // Call raw callback if set
    if (configuration.on_raw_data)
    {
      configuration.on_raw_data({words, word_count}, timestamp);
    }

    // Call message callback for each complete UMP
    if (configuration.on_message)
    {
      size_t offset = 0;
      while (offset < word_count)
      {
        ump msg;
        msg.data[0] = words[offset];

        // Determine message size from type
        uint8_t type = (msg.data[0] >> 28) & 0x0F;
        size_t msg_words = 1;
        switch (type)
        {
          case 0x0: // Utility
          case 0x1: // System
          case 0x2: // MIDI 1.0 Channel Voice
          case 0x6:
          case 0x7:
            msg_words = 1;
            break;
          case 0x3: // Data 64-bit
          case 0x4: // MIDI 2.0 Channel Voice
          case 0x8:
          case 0x9:
          case 0xA:
            msg_words = 2;
            break;
          case 0xB:
          case 0xC:
            msg_words = 3;
            break;
          case 0x5: // Data 128-bit
          case 0xD: // Flex Data
          case 0xE:
          case 0xF: // Stream
            msg_words = 4;
            break;
        }

        if (offset + msg_words > word_count)
          break;

        for (size_t i = 1; i < msg_words && i < 4; ++i)
          msg.data[i] = words[offset + i];

        msg.timestamp = timestamp;
        configuration.on_message(std::move(msg));

        offset += msg_words;
      }
    }
  }

  snd_ump_t* midiport_in_{};
  snd_ump_t* midiport_out_{};
  std::vector<pollfd> fds_;
  #endif
};

/// Threaded implementation with internal polling
class endpoint_impl_threaded final : public endpoint_impl_base
{
public:
  endpoint_impl_threaded(
      libremidi::remote_ump_endpoint_configuration&& conf,
      alsa_raw_ump::endpoint_api_configuration&& apiconf)
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
    // Get device name from endpoint info
    const std::string* device_name = std::get_if<std::string>(&endpoint.endpoint_id);
    if (!device_name)
      return std::errc::invalid_argument;

    // Determine if we need input/output based on function blocks
    bool need_input = endpoint.has_input() && configuration.on_message;
    bool need_output = endpoint.has_output();

    if (auto err = do_open(device_name->c_str(), need_input, need_output); err.is_set())
      return err;

    connected_endpoint_ = endpoint;

    // Start reading thread if we have input capability and callbacks
    if (midiport_in_ && (configuration.on_message || configuration.on_raw_data))
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
      if (jr_timestamps_enabled_)
      {
        m_thread = std::thread{[this] {
          run_thread(&endpoint_impl_base::read_input_buffer_with_timestamps);
        }};
      }
      else
      {
        m_thread = std::thread{[this] { run_thread(&endpoint_impl_base::read_input_buffer); }};
      }
      return stdx::error{};
    }
    catch (const std::system_error& e)
    {
      libremidi_handle_error(
          this->configuration,
          "error starting MIDI input thread: " + std::string(e.what()));
      return e.code();
    }
  }

  void run_thread(auto parse_func)
  {
    fds_.push_back(m_termination_event);
    const auto period
        = std::chrono::duration_cast<std::chrono::milliseconds>(configuration.poll_period).count();

    for (;;)
    {
      ssize_t err = poll(fds_.data(), fds_.size(), static_cast<int32_t>(period));
      if (err == -EAGAIN)
        continue;
      else if (err < 0)
        return;
      else if (m_termination_event.ready(fds_.back()))
        break;

      err = do_read_events(parse_func, {fds_.data(), fds_.size() - 1});
      if (err == -EAGAIN)
        continue;
      else if (err < 0)
        return;
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
      alsa_raw_ump::endpoint_api_configuration&& apiconf)
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
    const std::string* device_name = std::get_if<std::string>(&endpoint.endpoint_id);
    if (!device_name)
      return std::errc::invalid_argument;

    bool need_input = endpoint.has_input() && configuration.on_message;
    bool need_output = endpoint.has_output();

    if (auto err = do_open(device_name->c_str(), need_input, need_output); err.is_set())
      return err;

    connected_endpoint_ = endpoint;

    // Send poll callback for external event loop
    if (midiport_in_ && configuration.manual_poll)
    {
      send_poll_callback();
    }

    return stdx::error{};
  #else
    return std::errc::function_not_supported;
  #endif
  }

private:
  #if LIBREMIDI_ALSA_HAS_UMP
  void send_poll_callback()
  {
    if (jr_timestamps_enabled_)
    {
      configuration.manual_poll(manual_poll_parameters{
          .fds = {fds_.data(), fds_.size()},
          .callback = [this](std::span<pollfd> fds) {
            return do_read_events(&endpoint_impl_base::read_input_buffer_with_timestamps, fds);
          }});
    }
    else
    {
      configuration.manual_poll(manual_poll_parameters{
          .fds = {fds_.data(), fds_.size()},
          .callback = [this](std::span<pollfd> fds) {
            return do_read_events(&endpoint_impl_base::read_input_buffer, fds);
          }});
    }
  }
  #endif
};

} // namespace libremidi::alsa_raw_ump

namespace libremidi
{

/// Factory function for creating ALSA Raw UMP endpoints
template <>
inline std::unique_ptr<ump_endpoint_api> make<alsa_raw_ump::endpoint_impl_threaded>(
    libremidi::remote_ump_endpoint_configuration&& conf,
    libremidi::alsa_raw_ump::endpoint_api_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<alsa_raw_ump::endpoint_impl_manual>(std::move(conf), std::move(api));
  else
    return std::make_unique<alsa_raw_ump::endpoint_impl_threaded>(std::move(conf), std::move(api));
}
}
#endif
