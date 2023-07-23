#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
class midi_in_alsa
    : public midi_in_api
    , protected alsa_data
    , public error_handler
{
public:
  struct
      : input_configuration
      , alsa_sequencer_input_configuration
  {
  } configuration;

  bool require_timestamps() const noexcept
  {
    switch (configuration.timestamps)
    {
      case input_configuration::timestamp_mode::NoTimestamp:
      case input_configuration::timestamp_mode::SystemMonotonic:
        return false;
      case input_configuration::timestamp_mode::Absolute:
      case input_configuration::timestamp_mode::Relative:
        return true;
    }
    return true;
  }

  explicit midi_in_alsa(input_configuration&& conf, alsa_sequencer_input_configuration&& apiconf)
      : midi_in_api{}
      , configuration{std::move(conf), std::move(apiconf)}
  {
    // Set up the ALSA sequencer client.
    int result = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (result < 0)
    {
      error<driver_error>(
          this->configuration,
          "midi_in_alsa::initialize: error creating ALSA sequencer client "
          "object.");
      return;
    }

    // Set client name.
    snd_seq_set_client_name(seq, configuration.client_name.data());

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

  ~midi_in_alsa() override
  {
    // Cleanup.
    if (this->vport >= 0)
      snd_seq_delete_port(this->seq, this->vport);

    if (require_timestamps())
      snd_seq_free_queue(this->seq, this->queue_id);

    snd_midi_event_free(coder);

    snd_seq_close(this->seq);
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::LINUX_ALSA_SEQ;
  }

  [[nodiscard]] bool create_port(std::string_view portName)
  {
    if (this->vport < 0)
    {
      snd_seq_port_info_t* pinfo{};
      snd_seq_port_info_alloca(&pinfo);

      snd_seq_port_info_set_client(pinfo, 0);
      snd_seq_port_info_set_port(pinfo, 0);
      snd_seq_port_info_set_capability(
          pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
      snd_seq_port_info_set_type(
          pinfo, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
      snd_seq_port_info_set_midi_channels(pinfo, 16);

      if (require_timestamps())
      {
        snd_seq_port_info_set_timestamping(pinfo, 1);
        snd_seq_port_info_set_timestamp_real(pinfo, 1);
        snd_seq_port_info_set_timestamp_queue(pinfo, this->queue_id);
      }
      snd_seq_port_info_set_name(pinfo, portName.data());
      this->vport = snd_seq_create_port(this->seq, pinfo);

      if (this->vport < 0)
      {
        error<driver_error>(
            this->configuration, "midi_in_alsa::open_port: ALSA error creating input port.");
        return false;
      }
      this->vport = snd_seq_port_info_get_port(pinfo);
    }
    return true;
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

  void unsubscribe()
  {
    if (this->subscription)
    {
      snd_seq_unsubscribe_port(this->seq, this->subscription);
      snd_seq_port_subscribe_free(this->subscription);
      this->subscription = nullptr;
    }
  }

  int connect_port(snd_seq_addr_t sender)
  {
    snd_seq_addr_t receiver{};
    receiver.client = snd_seq_client_id(this->seq);
    receiver.port = this->vport;

    // Create the connection between ports
    // Make subscription
    if (int err = snd_seq_port_subscribe_malloc(&this->subscription); err < 0)
    {
      error<driver_error>(
          this->configuration,
          "midi_in_alsa::open_port: ALSA error allocation port subscription.");
      return err;
    }
    snd_seq_port_subscribe_set_sender(this->subscription, &sender);
    snd_seq_port_subscribe_set_dest(this->subscription, &receiver);
    if (int err = snd_seq_subscribe_port(this->seq, this->subscription); err != 0)
    {
      snd_seq_port_subscribe_free(this->subscription);
      this->subscription = nullptr;
      error<driver_error>(
          this->configuration, "midi_in_alsa::open_port: ALSA error making port connection.");
      return err;
    }
    return 0;
  }

  std::optional<snd_seq_addr_t> get_port_info(unsigned int portNumber)
  {
    snd_seq_port_info_t* src_pinfo{};
    snd_seq_port_info_alloca(&src_pinfo);

    if (alsa_seq::port_info(
            this->seq, src_pinfo, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, portNumber)
        == 0)
    {
      error<invalid_parameter_error>(
          this->configuration,
          "midi_in_alsa::open_port: invalid 'portNumber' argument: " + std::to_string(portNumber));
      return {};
    }

    snd_seq_addr_t addr;
    addr.client = snd_seq_port_info_get_client(src_pinfo);
    addr.port = snd_seq_port_info_get_port(src_pinfo);
    return addr;
  }

  int init_port(unsigned int portNumber, std::string_view portName)
  {
    if (connected_)
    {
      warning(this->configuration, "midi_in_alsa::open_port: a valid connection already exists!");
      return -1;
    }

    auto source_addr = get_port_info(portNumber);
    if (!source_addr)
      return -1;

    if (!create_port(portName))
      return -1;

    connect_port(*source_addr);
    start_queue();

    connected_ = true;
    return 0;
  }

  int init_virtual_port(std::string_view portName)
  {
    if (connected_)
    {
      warning(
          this->configuration,
          "midi_in_alsa::open_virtual_port: a valid connection already exists!");
      return -1;
    }

    if (!create_port(portName))
      return -1;

    start_queue();
    return 0;
  }

  void close_port() override
  {
    unsubscribe();
    stop_queue();
    connected_ = false;
  }

  void set_client_name(std::string_view clientName) override
  {
    alsa_data::set_client_name(clientName);
  }

  void set_port_name(std::string_view portName) override { alsa_data::set_port_name(portName); }

  unsigned int get_port_count() const override
  {
    return alsa_data::get_port_count(SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ);
  }

  std::string get_port_name(unsigned int portNumber) const override
  {
    return alsa_data::get_port_name(
        portNumber, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ);
  }

protected:
  void set_timestamp(snd_seq_event_t& ev, libremidi::message& msg) noexcept
  {
    static constexpr int64_t nanos = 1e9;
    switch (configuration.timestamps)
    {
      case input_configuration::NoTimestamp:
        msg.timestamp = 0;
        return;
      case input_configuration::Relative: {
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
      case input_configuration::Absolute: {
        msg.timestamp = ev.time.time.tv_sec * nanos + ev.time.time.tv_nsec;
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

class midi_in_alsa_threaded : public midi_in_alsa
{
public:
  midi_in_alsa_threaded(input_configuration&& conf, alsa_sequencer_input_configuration&& apiconf)
      : midi_in_alsa{std::move(conf), std::move(apiconf)}
  {
    this->dummy_thread_id = pthread_self();
    this->thread = this->dummy_thread_id;
    this->trigger_fds[0] = -1;
    this->trigger_fds[1] = -1;

    if (pipe(this->trigger_fds) == -1)
    {
      error<driver_error>(
          this->configuration, "midi_in_alsa::initialize: error creating pipe objects.");
    }
  }

  ~midi_in_alsa_threaded()
  {
    this->close_port();

    close(this->trigger_fds[0]);
    close(this->trigger_fds[1]);
  }

private:
  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (init_port(portNumber, portName) < 0)
      return;

    if (this->running == false)
    {
      if (!start_thread())
        return;
    }
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (init_virtual_port(portName) < 0)
      return;

    if (this->running == false)
    {
      if (!start_thread())
        return;
    }
  }

  void close_port() override
  {
    midi_in_alsa::close_port();

    stop_thread();
  }

  [[nodiscard]] int start_thread()
  {
    // Start our MIDI input thread.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);

    this->running = true;
    int err = pthread_create(&this->thread, &attr, alsaMidiHandler, this);
    pthread_attr_destroy(&attr);
    if (err)
    {
      unsubscribe();

      this->running = false;
      error<thread_error>(
          this->configuration, "midi_in_alsa::start_thread: error starting MIDI input thread!");
      return false;
    }
    return true;
  }

  void stop_thread()
  {
    // Stop thread to avoid triggering the callback, while the port is intended
    // to be closed
    if (this->running)
    {
      this->running = false;
      write(this->trigger_fds[1], &this->running, sizeof(this->running));

      if (!pthread_equal(this->thread, this->dummy_thread_id))
        pthread_join(this->thread, nullptr);
    }
  }

  static void* alsaMidiHandler(void* ptr)
  {
    auto& self = *static_cast<midi_in_alsa_threaded*>(ptr);

    int poll_fd_count{};
    pollfd* poll_fds{};

    poll_fd_count = snd_seq_poll_descriptors_count(self.seq, POLLIN) + 1;
    poll_fds = (struct pollfd*)alloca(poll_fd_count * sizeof(struct pollfd));
    snd_seq_poll_descriptors(self.seq, poll_fds + 1, poll_fd_count - 1, POLLIN);
    poll_fds[0].fd = self.trigger_fds[0];
    poll_fds[0].events = POLLIN;

    while (self.running)
    {
      if (snd_seq_event_input_pending(self.seq, 1) == 0)
      {
        // No data pending
        if (poll(poll_fds, poll_fd_count, -1) >= 0)
        {
          if (poll_fds[0].revents & POLLIN)
          {
            bool dummy;
            read(poll_fds[0].fd, &dummy, sizeof(dummy));
          }
        }
        continue;
      }

      int res = self.process_events();
#if defined(__LIBREMIDI_DEBUG__)
      if (res < 0)
        std::cerr << "midi_in_alsa::alsaMidiHandler: MIDI input error: " << strerror(res) << "\n";
#endif
    }

    self.thread = self.dummy_thread_id;
    return nullptr;
  }

  pthread_t thread{};
  pthread_t dummy_thread_id{};
  int trigger_fds[2]{};
  bool running{false};
};

class midi_in_alsa_manual : public midi_in_alsa
{
  using midi_in_alsa::midi_in_alsa;

  void init_fds()
  {
    auto poll_fd_count = snd_seq_poll_descriptors_count(seq, POLLIN);
    fds_.resize(poll_fd_count);
    snd_seq_poll_descriptors(seq, fds_.data(), poll_fd_count, POLLIN);

    configuration.manual_poll(
        manual_poll_parameters{.fds = this->fds_, .callback = [this](std::span<pollfd> fds) {
                                 return do_read_events();
                               }});
  }

  int do_read_events()
  {
    if (snd_seq_event_input_pending(seq, 1) == 0)
      return 0;

    return process_events();
  }

  void open_port(unsigned int portNumber, std::string_view name) override
  {
    if (init_port(portNumber, name) < 0)
      return;

    init_fds();

    connected_ = true;
  }

  void open_virtual_port(std::string_view name) override
  {
    if (init_virtual_port(name) < 0)
      return;

    init_fds();
  }

  std::vector<pollfd> fds_;
};

template <>
inline std::unique_ptr<midi_in_api> make<midi_in_alsa>(
    libremidi::input_configuration&& conf, libremidi::alsa_sequencer_input_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<midi_in_alsa_manual>(std::move(conf), std::move(api));
  else
    return std::make_unique<midi_in_alsa_threaded>(std::move(conf), std::move(api));
}
}
