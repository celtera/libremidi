#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi::alsa_seq
{
template <typename ConfigurationImpl>
using midi_in_base
    = std::conditional_t<ConfigurationImpl::midi_version == 1, midi1::in_api, midi2::in_api>;

template <typename ConfigurationBase, typename ConfigurationImpl>
class midi_in_impl
    : public midi_in_base<ConfigurationImpl>
    , protected alsa_data
    , public error_handler
{
public:
  struct
      : ConfigurationBase
      , ConfigurationImpl
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

  explicit midi_in_impl(ConfigurationBase&& conf, ConfigurationImpl&& apiconf)
      : midi_in_base<ConfigurationImpl>{}
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
      this->queue_id = snd.seq.alloc_queue(seq);
      // Set arbitrary tempo (mm=100) and resolution (240)
      snd_seq_queue_tempo_t* qtempo{};
      snd_seq_queue_tempo_alloca(&qtempo);
      snd.seq.queue_tempo_set_tempo(qtempo, 600000);
      snd.seq.queue_tempo_set_ppq(qtempo, 240);
      snd.seq.set_queue_tempo(this->seq, this->queue_id, qtempo);
      snd.seq.drain_output(this->seq);
    }

    // Create the event -> midi encoder
    {
      int result = snd.midi.event_new(0, &coder);
      if (result < 0)
      {
        error<driver_error>(
            this->configuration, "midi_in_alsa::initialize: error during snd_midi_event_new.");
        return;
      }
      snd.midi.event_init(coder);
      snd.midi.event_no_status(coder, 1);
    }
  }

  ~midi_in_impl() override
  {
    // Cleanup.
    if (this->vport >= 0)
      snd.seq.delete_port(this->seq, this->vport);

    if (require_timestamps())
      snd.seq.free_queue(this->seq, this->queue_id);

    snd.midi.event_free(coder);

    // Close if we do not have an user-provided client object
    if (!configuration.context)
      snd.seq.close(this->seq);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_SEQ; }

  [[nodiscard]] int create_port(std::string_view portName)
  {
    return alsa_data::create_port(
        *this, portName, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION,
        require_timestamps() ? std::optional<int>{this->queue_id} : std::nullopt);
  }

  void start_queue()
  {
    if (require_timestamps())
    {
      snd.seq.control_queue(this->seq, this->queue_id, SND_SEQ_EVENT_START, 0, nullptr);
      snd.seq.drain_output(this->seq);
    }
  }

  void stop_queue()
  {
    if (require_timestamps())
    {
      snd.seq.control_queue(this->seq, this->queue_id, SND_SEQ_EVENT_STOP, 0, nullptr);
      snd.seq.drain_output(this->seq);
    }
  }

  int connect_port(snd_seq_addr_t sender)
  {
    snd_seq_addr_t receiver{};
    receiver.client = snd.seq.client_id(this->seq);
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

    if (int ret = create_port(portName); ret < 0)
    {
      error<driver_error>(configuration, "midi_in_alsa::create_port: ALSA error creating port.");
      return ret;
    }

    if (int ret = connect_port(*source); ret < 0)
    {
      error<driver_error>(
          configuration, "midi_in_alsa::create_port: ALSA error making port connection.");
      return ret;
    }

    start_queue();

    return 0;
  }

  int init_virtual_port(std::string_view portName)
  {
    this->close_port();

    if (int ret = create_port(portName); ret < 0)
      return ret;

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
  void set_timestamp(const auto& ev, auto& msg) noexcept
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

        if (this->firstMessage == true)
        {
          this->firstMessage = false;
          msg.timestamp = 0;
        }
        else
        {
          msg.timestamp = time;
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

  int process_event(const snd_seq_event_t& ev)
  {
    if (!this->continueSysex)
      this->message.bytes.clear();

    // Filter the message types before any decoding
    switch (ev.type)
    {
      case SND_SEQ_EVENT_PORT_SUBSCRIBED:
      case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
        return 0;

      case SND_SEQ_EVENT_QFRAME: // MIDI time code
      case SND_SEQ_EVENT_TICK:   // 0xF9 ... MIDI timing tick
      case SND_SEQ_EVENT_CLOCK:  // 0xF8 ... MIDI timing (clock) tick
        if (configuration.ignore_timing)
          return 0;
        break;

      case SND_SEQ_EVENT_SENSING: // Active sensing
        if (configuration.ignore_sensing)
          return 0;
        break;

      case SND_SEQ_EVENT_SYSEX: {
        if (configuration.ignore_sysex)
          return 0;
        else if (ev.data.ext.len > decoding_buffer.size())
          decoding_buffer.resize(ev.data.ext.len);
        break;
      }
    }

    // Decode the message
    auto buf = decoding_buffer.data();
    auto buf_space = decoding_buffer.size();
    const uint64_t avail = snd.midi.event_decode(coder, buf, buf_space, &ev);
    if (avail > 0)
    {
      // The ALSA sequencer has a maximum buffer size for MIDI sysex
      // events of 256 bytes.  If a device sends sysex messages larger
      // than this, they are segmented into 256 byte chunks.  So,
      // we'll watch for this and concatenate sysex chunks into a
      // single sysex message if necessary.
      assert(avail < buf_space);
      if (!this->continueSysex)
        this->message.bytes.assign(buf, buf + avail);
      else
        this->message.bytes.insert(this->message.bytes.end(), buf, buf + avail);

      this->continueSysex
          = ((ev.type == SND_SEQ_EVENT_SYSEX) && (this->message.bytes.back() != 0xF7));
      if (!this->continueSysex)
      {
        set_timestamp(ev, this->message);
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

    if (this->message.bytes.size() == 0 || this->continueSysex)
      return 0;

    // Finally the message is ready
    configuration.on_message(std::move(this->message));
    this->message.clear();
    return 0;
  }

  int process_events()
  {
    snd_seq_event_t* ev{};
    event_handle handle{snd};
    int result = 0;
    while ((result = snd.seq.event_input(seq, &ev)) > 0)
    {
      handle.reset(ev);
      if (int err = process_event(*ev); err < 0)
        return err;
    }
    return result;
  }

#if __has_include(<alsa/ump.h>)
  int process_ump_event(const snd_seq_ump_event_t& ev)
  {
    // Filter the message types before any decoding
    switch (ev.type)
    {
      case SND_SEQ_EVENT_PORT_SUBSCRIBED:
      case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
        return 0;

      case SND_SEQ_EVENT_QFRAME: // MIDI time code
      case SND_SEQ_EVENT_TICK:   // 0xF9 ... MIDI timing tick
      case SND_SEQ_EVENT_CLOCK:  // 0xF8 ... MIDI timing (clock) tick
        if (configuration.ignore_timing)
          return 0;
        break;

      case SND_SEQ_EVENT_SENSING: // Active sensing
        if (configuration.ignore_sensing)
          return 0;
        break;

      case SND_SEQ_EVENT_SYSEX: {
        if (configuration.ignore_sysex)
          return 0;
        break;
      }
    }

    // MIDI 2 : no decoder, we can just send the UMP data directly, yay
    libremidi::ump ump;
    std::memcpy(ump.bytes, ev.ump, sizeof(ev.ump));
    set_timestamp(ev, ump);
    configuration.on_message(std::move(ump));
    return 0;
  }

  int process_ump_events()
  {
    snd_seq_ump_event_t* ev{};
    event_handle handle{snd};
    int result = 0;
    while ((result = snd.seq.ump.event_input(seq, &ev)) > 0)
    {
      handle.reset((snd_seq_event_t*)ev);
      if (int err = process_ump_event(*ev); err < 0)
        return err;
    }
    return result;
  }
#endif

  int queue_id{}; // an input queue is needed to get timestamped events
  snd_seq_real_time_t last_time{};

  // Only needed for midi 1
  std::vector<unsigned char> decoding_buffer = std::vector<unsigned char>(32);
};

template <typename ConfigurationBase, typename ConfigurationImpl>
class midi_in_alsa_threaded : public midi_in_impl<ConfigurationBase, ConfigurationImpl>
{
public:
  midi_in_alsa_threaded(ConfigurationBase&& conf, ConfigurationImpl&& apiconf)
      : midi_in_impl<ConfigurationBase, ConfigurationImpl>{std::move(conf), std::move(apiconf)}
  {
    if (this->termination_event < 0)
    {
      this->template error<driver_error>(
          this->configuration, "midi_in_alsa::initialize: error creating eventfd.");
    }
  }

  ~midi_in_alsa_threaded() { this->close_port(); }

private:
  bool open_port(const input_port& pt, std::string_view local_port_name) override
  {
    if (int err = this->init_port(this->to_address(pt), local_port_name); err < 0)
      return false;

    if (!start_thread())
      return false;

    return true;
  }

  bool open_virtual_port(std::string_view portName) override
  {
    if (int err = this->init_virtual_port(portName); err < 0)
      return false;

    if (!this->start_thread())
      return false;
    return true;
  }

  void close_port() override
  {
    midi_in_impl<ConfigurationBase, ConfigurationImpl>::close_port();

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
      this->unsubscribe();

      this->template error<thread_error>(
          this->configuration,
          "midi_in_alsa::start_thread: error starting MIDI input thread: "s + e.what());
      return false;
    }
    return true;
  }

  void stop_thread()
  {
    termination_event.notify();

    if (this->thread.joinable())
      this->thread.join();

    termination_event.consume();
  }

  void thread_handler()
  {
    int poll_fd_count = alsa_data::snd.seq.poll_descriptors_count(this->seq, POLLIN) + 1;
    auto poll_fds = (struct pollfd*)alloca(poll_fd_count * sizeof(struct pollfd));
    poll_fds[0] = this->termination_event;
    alsa_data::snd.seq.poll_descriptors(this->seq, poll_fds + 1, poll_fd_count - 1, POLLIN);

    for (;;)
    {
      if (alsa_data::snd.seq.event_input_pending(this->seq, 1) == 0)
      {
        // No data pending
        if (poll(poll_fds, poll_fd_count, -1) >= 0)
        {
          // We got our stop-thread signal
          if (termination_event.ready(poll_fds[0]))
          {
            break;
          }
        }
        continue;
      }

      int res{};
      if constexpr (ConfigurationImpl::midi_version == 1)
      {
        res = this->process_events();
      }
#if __has_include(<alsa/ump.h>)
      else if constexpr (ConfigurationImpl::midi_version == 2)
      {
        res = this->process_ump_events();
      }
#endif

      (void)res;
#if defined(__LIBREMIDI_DEBUG__)
      if (res < 0)
        std::cerr << "midi_in_alsa::thread_handler: MIDI input error: " << snd.strerror(res)
                  << "\n";
#endif
    }
  }

  std::thread thread{};
  eventfd_notifier termination_event{};
};

template <typename ConfigurationBase, typename ConfigurationImpl>
class midi_in_alsa_manual : public midi_in_impl<ConfigurationBase, ConfigurationImpl>
{
public:
  midi_in_alsa_manual(ConfigurationBase&& conf, ConfigurationImpl&& apiconf)
      : midi_in_impl<ConfigurationBase, ConfigurationImpl>{std::move(conf), std::move(apiconf)}
  {
    assert(this->configuration.manual_poll);
    assert(this->configuration.stop_poll);
  }

  [[nodiscard]] int init_callback()
  {
    using poll_params = typename ConfigurationImpl::poll_parameters_type;
    this->configuration.manual_poll(
        poll_params{.addr = this->vaddr, .callback = [this](const auto& ev) {
                      if constexpr (ConfigurationImpl::midi_version == 1)
                        return this->process_event(ev);
#if __has_include(<alsa/ump.h>)
                      else
                        return this->process_ump_event(ev);
#endif
                    }});
    return 0;
  }

  ~midi_in_alsa_manual() { this->close_port(); }

  bool open_port(const input_port& pt, std::string_view local_port_name) override
  {
    if (int err = this->init_port(this->to_address(pt), local_port_name); err < 0)
      return false;

    if (int err = init_callback(); err < 0)
      return false;
    return true;
  }

  bool open_virtual_port(std::string_view name) override
  {
    if (int err = this->init_virtual_port(name); err < 0)
      return false;

    if (int err = init_callback(); err < 0)
      return false;
    return true;
  }

  void close_port() override
  {
    this->configuration.stop_poll(this->vaddr);

    midi_in_impl<ConfigurationBase, ConfigurationImpl>::close_port();
  }
};
}

namespace libremidi
{
template <>
inline std::unique_ptr<midi_in_api>
make<alsa_seq::midi_in_impl<libremidi::input_configuration, alsa_seq::input_configuration>>(
    libremidi::input_configuration&& conf, libremidi::alsa_seq::input_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<alsa_seq::midi_in_alsa_manual<
        libremidi::input_configuration, alsa_seq::input_configuration>>(
        std::move(conf), std::move(api));
  else
    return std::make_unique<alsa_seq::midi_in_alsa_threaded<
        libremidi::input_configuration, alsa_seq::input_configuration>>(
        std::move(conf), std::move(api));
}
}
