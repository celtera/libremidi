#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
class midi_in_alsa final
    : public midi_in_api
    , private alsa_data
    , public error_handler
{
public:
  struct
      : input_configuration
      , alsa_sequencer_input_configuration
  {
  } configuration;

  explicit midi_in_alsa(input_configuration&& conf, alsa_sequencer_input_configuration&& apiconf)
      : midi_in_api{}
      , configuration{std::move(conf), std::move(apiconf)}
  {
    // Set up the ALSA sequencer client.
    snd_seq_t* seq{};
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

    // Save our api-specific connection information.
    this->seq = seq;
    this->vport = -1;
    this->subscription = nullptr;
    this->dummy_thread_id = pthread_self();
    this->thread = this->dummy_thread_id;
    this->trigger_fds[0] = -1;
    this->trigger_fds[1] = -1;

    if (pipe(this->trigger_fds) == -1)
    {
      error<driver_error>(
          this->configuration, "midi_in_alsa::initialize: error creating pipe objects.");
      return;
    }

// Create the input queue
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
    this->queue_id = snd_seq_alloc_named_queue(seq, "libremidi queue");
    // Set arbitrary tempo (mm=100) and resolution (240)
    snd_seq_queue_tempo_t* qtempo;
    snd_seq_queue_tempo_alloca(&qtempo);
    snd_seq_queue_tempo_set_tempo(qtempo, 600000);
    snd_seq_queue_tempo_set_ppq(qtempo, 240);
    snd_seq_set_queue_tempo(this->seq, this->queue_id, qtempo);
    snd_seq_drain_output(this->seq);
#endif
  }

  ~midi_in_alsa() override
  {
    // Close a connection if it exists.
    midi_in_alsa::close_port();

    // Shutdown the input thread.
    if (this->doInput)
    {
      this->doInput = false;
      write(this->trigger_fds[1], &this->doInput, sizeof(this->doInput));

      if (!pthread_equal(this->thread, this->dummy_thread_id))
        pthread_join(this->thread, nullptr);
    }

    // Cleanup.
    close(this->trigger_fds[0]);
    close(this->trigger_fds[1]);
    if (this->vport >= 0)
      snd_seq_delete_port(this->seq, this->vport);
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
    snd_seq_free_queue(this->seq, this->queue_id);
#endif
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
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
      snd_seq_port_info_set_timestamping(pinfo, 1);
      snd_seq_port_info_set_timestamp_real(pinfo, 1);
      snd_seq_port_info_set_timestamp_queue(pinfo, this->queue_id);
#endif
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

  [[nodiscard]] bool start_thread()
  {
// Start the input queue
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
    snd_seq_start_queue(this->seq, this->queue_id, nullptr);
    snd_seq_drain_output(this->seq);
#endif
    // Start our MIDI input thread.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);

    this->doInput = true;
    int err = pthread_create(&this->thread, &attr, alsaMidiHandler, this);
    pthread_attr_destroy(&attr);
    if (err)
    {
      if (this->subscription)
      {
        snd_seq_unsubscribe_port(this->seq, this->subscription);
        snd_seq_port_subscribe_free(this->subscription);
        this->subscription = nullptr;
      }
      this->doInput = false;
      error<thread_error>(
          this->configuration, "midi_in_alsa::start_thread: error starting MIDI input thread!");
      return false;
    }
    return true;
  }

  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (connected_)
    {
      warning(this->configuration, "midi_in_alsa::open_port: a valid connection already exists!");
      return;
    }

    // Find the port to which we want to connect to
    unsigned int nSrc = this->get_port_count();
    if (nSrc < 1)
    {
      error<no_devices_found_error>(
          this->configuration, "midi_in_alsa::open_port: no MIDI input sources found!");
      return;
    }

    snd_seq_port_info_t* src_pinfo{};
    snd_seq_port_info_alloca(&src_pinfo);
    if (alsa_seq::port_info(
            this->seq, src_pinfo, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            (int)portNumber)
        == 0)
    {
      error<invalid_parameter_error>(
          this->configuration,
          "midi_in_alsa::open_port: invalid 'portNumber' argument: " + std::to_string(portNumber));
      return;
    }

    snd_seq_addr_t sender{}, receiver{};
    sender.client = snd_seq_port_info_get_client(src_pinfo);
    sender.port = snd_seq_port_info_get_port(src_pinfo);
    receiver.client = snd_seq_client_id(this->seq);

    if (!create_port(portName))
      return;

    receiver.port = this->vport;

    // Create the connection between ports
    if (!this->subscription)
    {
      // Make subscription
      if (snd_seq_port_subscribe_malloc(&this->subscription) < 0)
      {
        error<driver_error>(
            this->configuration,
            "midi_in_alsa::open_port: ALSA error allocation port subscription.");
        return;
      }
      snd_seq_port_subscribe_set_sender(this->subscription, &sender);
      snd_seq_port_subscribe_set_dest(this->subscription, &receiver);
      if (snd_seq_subscribe_port(this->seq, this->subscription))
      {
        snd_seq_port_subscribe_free(this->subscription);
        this->subscription = nullptr;
        error<driver_error>(
            this->configuration, "midi_in_alsa::open_port: ALSA error making port connection.");
        return;
      }
    }

    // Run
    if (this->doInput == false)
    {
      if (!start_thread())
        return;
    }
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (!create_port(portName))
      return;

    if (this->doInput == false)
    {
      // Wait for old thread to stop, if still running
      if (!pthread_equal(this->thread, this->dummy_thread_id))
        pthread_join(this->thread, nullptr);

      if (!start_thread())
        return;
    }
    connected_ = true;
  }

  void close_port() override
  {
    if (connected_)
    {
      if (this->subscription)
      {
        snd_seq_unsubscribe_port(this->seq, this->subscription);
        snd_seq_port_subscribe_free(this->subscription);
        this->subscription = nullptr;
      }
      // Stop the input queue
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
      snd_seq_stop_queue(this->seq, this->queue_id, nullptr);
      snd_seq_drain_output(this->seq);
#endif
      connected_ = false;
    }

    // Stop thread to avoid triggering the callback, while the port is intended
    // to be closed
    if (this->doInput)
    {
      this->doInput = false;
      write(this->trigger_fds[1], &this->doInput, sizeof(this->doInput));

      if (!pthread_equal(this->thread, this->dummy_thread_id))
        pthread_join(this->thread, nullptr);
    }
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

private:
  void set_timestamp(snd_seq_event_t& ev, libremidi::message& msg) noexcept
  {
    static constexpr int64_t nanos = 1e9;
    switch (configuration.timestamps)
    {
      case input_configuration::NoTimestamp:
        msg.timestamp = 0;
        return;
      case input_configuration::Relative: {
        const auto t0 = int64_t(ev.time.time.tv_sec) * nanos + int64_t(ev.time.time.tv_nsec);
        const auto t1 = int64_t(last_time.tv_sec) * nanos + int64_t(last_time.tv_nsec);
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

  static void* alsaMidiHandler(void* ptr)
  {
    auto& self = *static_cast<midi_in_alsa*>(ptr);

    double time{};
    bool continueSysex = false;
    bool doDecode = false;
    auto& message = self.message;
    int poll_fd_count{};
    pollfd* poll_fds{};

    snd_seq_event_t* ev{};

    int result = snd_midi_event_new(0, &self.coder);
    if (result < 0)
    {
      self.doInput = false;
#if defined(__LIBREMIDI_DEBUG__)
      std::cerr << "\nmidi_in_alsa::alsaMidiHandler: error initializing MIDI "
                   "event parser!\n\n";
#endif
      return nullptr;
    }

    std::vector<unsigned char> buffer;
    buffer.resize(32); // Initial buffer size

    snd_midi_event_init(self.coder);
    snd_midi_event_no_status(self.coder, 1); // suppress running status messages

    poll_fd_count = snd_seq_poll_descriptors_count(self.seq, POLLIN) + 1;
    poll_fds = (struct pollfd*)alloca(poll_fd_count * sizeof(struct pollfd));
    snd_seq_poll_descriptors(self.seq, poll_fds + 1, poll_fd_count - 1, POLLIN);
    poll_fds[0].fd = self.trigger_fds[0];
    poll_fds[0].events = POLLIN;

    while (self.doInput)
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

      // If here, there should be this->
      result = snd_seq_event_input(self.seq, &ev);
      if (result == -ENOSPC)
      {
#if defined(__LIBREMIDI_DEBUG__)
        std::cerr << "\nmidi_in_alsa::alsaMidiHandler: MIDI input buffer overrun!\n\n";
#endif
        continue;
      }
      else if (result <= 0)
      {
#if defined(__LIBREMIDI_DEBUG__)
        std::cerr << "\nmidi_in_alsa::alsaMidiHandler: unknown MIDI input error!\n";
        perror("System reports");
#endif
        continue;
      }

      // This is a bit weird, but we now have to decode an ALSA MIDI
      // event (back) into MIDI bytes.  We'll ignore non-MIDI types.
      if (!continueSysex)
        message.bytes.clear();

      doDecode = false;
      switch (ev->type)
      {
        case SND_SEQ_EVENT_PORT_SUBSCRIBED:
#if defined(__LIBREMIDI_DEBUG__)
          std::cerr << "midi_in_alsa::alsaMidiHandler: port connection made!\n";
#endif
          break;

        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
#if defined(__LIBREMIDI_DEBUG__)
          std::cerr << "midi_in_alsa::alsaMidiHandler: port connection has closed!\n";
          std::cerr << "sender = " << (int)ev->data.connect.sender.client << ":"
                    << (int)ev->data.connect.sender.port
                    << ", dest = " << (int)ev->data.connect.dest.client << ":"
                    << (int)ev->data.connect.dest.port << std::endl;
#endif
          break;

        case SND_SEQ_EVENT_QFRAME: // MIDI time code
          if (!self.configuration.ignore_timing)
            doDecode = true;
          break;

        case SND_SEQ_EVENT_TICK: // 0xF9 ... MIDI timing tick
          if (!self.configuration.ignore_timing)
            doDecode = true;
          break;

        case SND_SEQ_EVENT_CLOCK: // 0xF8 ... MIDI timing (clock) tick
          if (!self.configuration.ignore_timing)
            doDecode = true;
          break;

        case SND_SEQ_EVENT_SENSING: // Active sensing
          if (!self.configuration.ignore_sensing)
            doDecode = true;
          break;

        case SND_SEQ_EVENT_SYSEX: {
          if (self.configuration.ignore_sysex)
            break;
          if (ev->data.ext.len > buffer.size())
          {
            buffer.resize(ev->data.ext.len);
          }
          doDecode = true;
          break;
        }

        default:
          doDecode = true;
      }

      if (doDecode)
      {
        uint64_t nBytes = snd_midi_event_decode(self.coder, buffer.data(), buffer.size(), ev);
        if (nBytes > 0)
        {
          // The ALSA sequencer has a maximum buffer size for MIDI sysex
          // events of 256 bytes.  If a device sends sysex messages larger
          // than this, they are segmented into 256 byte chunks.  So,
          // we'll watch for this and concatenate sysex chunks into a
          // single sysex message if necessary.
          assert(nBytes < buffer.size());
          if (!continueSysex)
            message.bytes.assign(buffer.data(), buffer.data() + nBytes);
          else
            message.bytes.insert(message.bytes.end(), buffer.data(), buffer.data() + nBytes);

          continueSysex = ((ev->type == SND_SEQ_EVENT_SYSEX) && (message.bytes.back() != 0xF7));
          if (!continueSysex)
          {
            self.set_timestamp(*ev, message);
          }
          else
          {
#if defined(__LIBREMIDI_DEBUG__)
            std::cerr << "\nmidi_in_alsa::alsaMidiHandler: event parsing error or "
                         "not a MIDI event!\n\n";
#endif
          }
        }
      }

      snd_seq_free_event(ev);
      if (message.bytes.size() == 0 || continueSysex)
        continue;

      self.configuration.on_message(std::move(message));
      message.clear();
    }

    snd_midi_event_free(self.coder);
    self.coder = nullptr;
    self.thread = self.dummy_thread_id;
    return nullptr;
  }

  pthread_t thread{};
  pthread_t dummy_thread_id{};
  int queue_id{}; // an input queue is needed to get timestamped events
  int trigger_fds[2]{};
  snd_seq_real_time_t last_time{};

  bool doInput{false};
};

}
