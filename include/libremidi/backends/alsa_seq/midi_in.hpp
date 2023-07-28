#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi::alsa_seq
{
class midi_in_impl
    : public midi1::in_api
    , protected alsa_data
    , public error_handler
{
public:
  struct
      : libremidi::input_configuration
      , alsa_seq::input_configuration
  {
  } configuration;

  bool require_timestamps() const noexcept
  {
    switch (configuration.timestamps)
    {
      case timestamp_mode::NoTimestamp:
      case timestamp_mode::SystemMonotonic:
        return false;
      case timestamp_mode::Absolute:
      case timestamp_mode::Relative:
        return true;
    }
    return true;
  }

  explicit midi_in_impl(
      libremidi::input_configuration&& conf, alsa_seq::input_configuration&& apiconf)
      : midi1::in_api{}
      , configuration{std::move(conf), std::move(apiconf)}
  {
    if (init_client(configuration) < 0)
    {
      error<driver_error>(
          this->configuration,
          "midi_in_alsa::initialize: error creating ALSA sequencer client "
          "object.");
      return;
    }

    // Create the input queue
    if (require_timestamps())
    {
      this->queue_id = snd_seq_alloc_queue(seq);
      // Set arbitrary tempo (mm=100) and resolution (240)
      snd_seq_queue_tempo_t* qtempo{};
      snd_seq_queue_tempo_alloca(&qtempo);
      snd_seq_queue_tempo_set_tempo(qtempo, 600000);
      snd_seq_queue_tempo_set_ppq(qtempo, 240);
      snd_seq_set_queue_tempo(this->seq, this->queue_id, qtempo);
      snd_seq_drain_output(this->seq);
    }

    // Create the event -> midi encoder
    {
      int result = snd_midi_event_new(0, &coder);
      if (result < 0)
      {
        error<driver_error>(
            this->configuration, "midi_in_alsa::initialize: error during snd_midi_event_new.");
        return;
      }
      snd_midi_event_init(coder);
      snd_midi_event_no_status(coder, 1);
    }
  }

  ~midi_in_impl() override
  {
    // Cleanup.
    if (this->vport >= 0)
      snd_seq_delete_port(this->seq, this->vport);

    if (require_timestamps())
      snd_seq_free_queue(this->seq, this->queue_id);

    snd_midi_event_free(coder);

    // Close if we do not have an user-provided client object
    if (!configuration.context)
      snd_seq_close(this->seq);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_SEQ; }

  [[nodiscard]] bool create_port(std::string_view portName)
  {
    return alsa_data::create_port(
               *this, portName, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
               require_timestamps() ? std::optional<int>{this->queue_id} : std::nullopt)
           >= 0;
  }

  void start_queue()
  {
    if (require_timestamps())
    {
      snd_seq_start_queue(this->seq, this->queue_id, nullptr);
      snd_seq_drain_output(this->seq);
    }
  }

  void stop_queue()
  {
    if (require_timestamps())
    {
      snd_seq_stop_queue(this->seq, this->queue_id, nullptr);
      snd_seq_drain_output(this->seq);
    }
  }

  int connect_port(snd_seq_addr_t sender)
  {
    snd_seq_addr_t receiver{};
    receiver.client = snd_seq_client_id(this->seq);
    receiver.port = this->vport;

    return create_connection(*this, sender, receiver, false);
  }

  std::optional<snd_seq_addr_t> to_address(const port_information& p)
  {
    return alsa_data::get_port_info(p);
  }

  int init_port(std::optional<snd_seq_addr_t> source, std::string_view portName)
  {
    this->close_port();

    if (!source)
      return -1;

    if (!create_port(portName))
      return -1;

    if (int ret = connect_port(*source); ret < 0)
      return ret;

    start_queue();

    return 0;
  }

  int init_virtual_port(std::string_view portName)
  {
    this->close_port();

    if (!create_port(portName))
      return -1;

    start_queue();
    return 0;
  }

  void close_port() override
  {
    unsubscribe();
    stop_queue();
  }

  void set_client_name(std::string_view clientName) override
  {
    alsa_data::set_client_name(clientName);
  }

  void set_port_name(std::string_view portName) override { alsa_data::set_port_name(portName); }

protected:
  void set_timestamp(snd_seq_event_t& ev, libremidi::message& msg) noexcept
  {
    static constexpr int64_t nanos = 1e9;
    switch (configuration.timestamps)
    {
      case timestamp_mode::NoTimestamp:
        msg.timestamp = 0;
        return;
      case timestamp_mode::Relative: {
        const auto t1 = int64_t(ev.time.time.tv_sec) * nanos + int64_t(ev.time.time.tv_nsec);
        const auto t0 = int64_t(last_time.tv_sec) * nanos + int64_t(last_time.tv_nsec);
        const auto time = t1 - t0;

        last_time = ev.time.time;

        if (firstMessage == true)
        {
          firstMessage = false;
          message.timestamp = 0;
        }
        else
        {
          message.timestamp = time;
        }
        return;
      }
      case timestamp_mode::Absolute: {
        msg.timestamp = ev.time.time.tv_sec * nanos + ev.time.time.tv_nsec;
        break;
      }
      case timestamp_mode::SystemMonotonic: {
        namespace clk = std::chrono;
        msg.timestamp
            = clk::duration_cast<clk::nanoseconds>(clk::steady_clock::now().time_since_epoch())
                  .count();
        break;
      }
    }
  }

  int process_events()
  {
    snd_seq_event_t* ev{};
    unique_handle<snd_seq_event_t, snd_seq_free_event> handle;
    int result = 0;
    while ((result = snd_seq_event_input(seq, &ev)) > 0)
    {
      handle.reset(ev);

      if (!continueSysex)
        message.bytes.clear();

      // Filter the message types before any decoding
      switch (ev->type)
      {
        case SND_SEQ_EVENT_PORT_SUBSCRIBED:
        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
          return 0;

        case SND_SEQ_EVENT_QFRAME: // MIDI time code
        case SND_SEQ_EVENT_TICK:   // 0xF9 ... MIDI timing tick
        case SND_SEQ_EVENT_CLOCK:  // 0xF8 ... MIDI timing (clock) tick
          if (configuration.ignore_timing)
            continue;
          break;

        case SND_SEQ_EVENT_SENSING: // Active sensing
          if (configuration.ignore_sensing)
            continue;
          break;

        case SND_SEQ_EVENT_SYSEX: {
          if (configuration.ignore_sysex)
            continue;
          else if (ev->data.ext.len > decoding_buffer.size())
            decoding_buffer.resize(ev->data.ext.len);
          break;
        }
      }

      // Decode the message
      auto buf = decoding_buffer.data();
      auto buf_space = decoding_buffer.size();
      const uint64_t avail = snd_midi_event_decode(coder, buf, buf_space, ev);
      if (avail > 0)
      {
        // The ALSA sequencer has a maximum buffer size for MIDI sysex
        // events of 256 bytes.  If a device sends sysex messages larger
        // than this, they are segmented into 256 byte chunks.  So,
        // we'll watch for this and concatenate sysex chunks into a
        // single sysex message if necessary.
        assert(avail < buf_space);
        if (!continueSysex)
          message.bytes.assign(buf, buf + avail);
        else
          message.bytes.insert(message.bytes.end(), buf, buf + avail);

        continueSysex = ((ev->type == SND_SEQ_EVENT_SYSEX) && (message.bytes.back() != 0xF7));
        if (!continueSysex)
        {
          set_timestamp(*ev, message);
        }
        else
        {
#if defined(__LIBREMIDI_DEBUG__)
          std::cerr << "\nmidi_in_alsa::alsaMidiHandler: event parsing error or "
                       "not a MIDI event!\n\n";
#endif
          return -EINVAL;
        }
      }

      if (message.bytes.size() == 0 || continueSysex)
        continue;

      // Finally the message is ready
      configuration.on_message(std::move(message));
      message.clear();
    }
    return result;
  }

  int queue_id{}; // an input queue is needed to get timestamped events
  snd_seq_real_time_t last_time{};

  std::vector<unsigned char> decoding_buffer = std::vector<unsigned char>(32);
};

class midi_in_alsa_threaded : public midi_in_impl
{
public:
  midi_in_alsa_threaded(
      libremidi::input_configuration&& conf, alsa_seq::input_configuration&& apiconf)
      : midi_in_impl{std::move(conf), std::move(apiconf)}
  {
    if (this->event_fd < 0)
    {
      error<driver_error>(
          this->configuration, "midi_in_alsa::initialize: error creating eventfd.");
    }
  }

  ~midi_in_alsa_threaded() { this->close_port(); }

private:
  bool open_port(const port_information& pt, std::string_view local_port_name) override
  {
    if (int err = init_port(to_address(pt), local_port_name); err < 0)
      return false;

    if (!start_thread())
      return false;

    return true;
  }

  bool open_virtual_port(std::string_view portName) override
  {
    if (int err = init_virtual_port(portName); err < 0)
      return false;

    if (!start_thread())
      return false;
    return true;
  }

  void close_port() override
  {
    midi_in_impl::close_port();

    stop_thread();
  }

  [[nodiscard]] int start_thread()
  {
    try
    {
      this->thread = std::thread([this] { thread_handler(); });
    }
    catch (const std::system_error& e)
    {
      using namespace std::literals;
      unsubscribe();

      error<thread_error>(
          this->configuration,
          "midi_in_alsa::start_thread: error starting MIDI input thread: "s + e.what());
      return false;
    }
    return true;
  }

  void stop_thread()
  {
    event_fd.notify();

    if (this->thread.joinable())
      this->thread.join();

    event_fd.consume();
  }

  void thread_handler()
  {
    int poll_fd_count = snd_seq_poll_descriptors_count(this->seq, POLLIN) + 1;
    auto poll_fds = (struct pollfd*)alloca(poll_fd_count * sizeof(struct pollfd));
    poll_fds[0] = this->event_fd;
    snd_seq_poll_descriptors(this->seq, poll_fds + 1, poll_fd_count - 1, POLLIN);

    for (;;)
    {
      if (snd_seq_event_input_pending(this->seq, 1) == 0)
      {
        // No data pending
        if (poll(poll_fds, poll_fd_count, -1) >= 0)
        {
          // We got our stop-thread signal
          if (event_fd.ready(poll_fds[0]))
          {
            break;
          }
        }
        continue;
      }

      int res = this->process_events();
      (void)res;
#if defined(__LIBREMIDI_DEBUG__)
      if (res < 0)
        std::cerr << "midi_in_alsa::thread_handler: MIDI input error: " << strerror(res) << "\n";
#endif
    }
  }

  std::thread thread{};
  eventfd_notifier event_fd{};
};

class midi_in_alsa_manual : public midi_in_impl
{
  using midi_in_impl::midi_in_impl;

  [[nodiscard]] int init_fds()
  {
    auto poll_fd_count = snd_seq_poll_descriptors_count(seq, POLLIN);
    fds_.resize(poll_fd_count);
    if (int err = snd_seq_poll_descriptors(seq, fds_.data(), poll_fd_count, POLLIN); err < 0)
      return err;

    configuration.manual_poll(manual_poll_parameters{
        .fds = {this->fds_.data(), this->fds_.size()},
        .callback = [this](std::span<pollfd> fds) { return do_read_events(); }});
    return 0;
  }

  int do_read_events()
  {
    if (snd_seq_event_input_pending(seq, 1) == 0)
      return 0;

    return process_events();
  }

  bool open_port(const port_information& pt, std::string_view local_port_name) override
  {
    if (int err = init_port(to_address(pt), local_port_name); err < 0)
      return false;

    if (int err = init_fds(); err < 0)
      return false;
    return true;
  }

  bool open_virtual_port(std::string_view name) override
  {
    if (int err = init_virtual_port(name); err < 0)
      return false;

    if (int err = init_fds(); err < 0)
      return false;
    return true;
  }

  std::vector<pollfd> fds_;
};
}

namespace libremidi
{
template <>
inline std::unique_ptr<midi_in_api> make<alsa_seq::midi_in_impl>(
    libremidi::input_configuration&& conf, libremidi::alsa_seq::input_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<alsa_seq::midi_in_alsa_manual>(std::move(conf), std::move(api));
  else
    return std::make_unique<alsa_seq::midi_in_alsa_threaded>(std::move(conf), std::move(api));
}
}
