#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
class midi_in_alsa final : public midi_in_api
{
public:
  explicit midi_in_alsa(std::string_view client_name)
      : midi_in_api{&data}
  {
    // Set up the ALSA sequencer client.
    snd_seq_t* seq;
    int result = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (result < 0)
    {
      error<driver_error>(
          "midi_in_alsa::initialize: error creating ALSA sequencer client "
          "object.");
      return;
    }

    // Set client name.
    snd_seq_set_client_name(seq, client_name.data());

    // Save our api-specific connection information.
    data.seq = seq;
    data.vport = -1;
    data.subscription = nullptr;
    data.dummy_thread_id = pthread_self();
    data.thread = data.dummy_thread_id;
    data.trigger_fds[0] = -1;
    data.trigger_fds[1] = -1;

    if (pipe(data.trigger_fds) == -1)
    {
      error<driver_error>("midi_in_alsa::initialize: error creating pipe objects.");
      return;
    }

// Create the input queue
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
    data.queue_id = snd_seq_alloc_named_queue(seq, "libremidi queue");
    // Set arbitrary tempo (mm=100) and resolution (240)
    snd_seq_queue_tempo_t* qtempo;
    snd_seq_queue_tempo_alloca(&qtempo);
    snd_seq_queue_tempo_set_tempo(qtempo, 600000);
    snd_seq_queue_tempo_set_ppq(qtempo, 240);
    snd_seq_set_queue_tempo(data.seq, data.queue_id, qtempo);
    snd_seq_drain_output(data.seq);
#endif
  }

  ~midi_in_alsa() override
  {
    // Close a connection if it exists.
    midi_in_alsa::close_port();

    // Shutdown the input thread.
    if (data.doInput)
    {
      data.doInput = false;
      write(data.trigger_fds[1], &data.doInput, sizeof(data.doInput));

      if (!pthread_equal(data.thread, data.dummy_thread_id))
        pthread_join(data.thread, nullptr);
    }

    // Cleanup.
    close(data.trigger_fds[0]);
    close(data.trigger_fds[1]);
    if (data.vport >= 0)
      snd_seq_delete_port(data.seq, data.vport);
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
    snd_seq_free_queue(data.seq, data.queue_id);
#endif
    snd_seq_close(data.seq);
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::LINUX_ALSA_SEQ;
  }

  [[nodiscard]] bool create_port(std::string_view portName)
  {
    if (data.vport < 0)
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
      snd_seq_port_info_set_timestamp_queue(pinfo, data.queue_id);
#endif
      snd_seq_port_info_set_name(pinfo, portName.data());
      data.vport = snd_seq_create_port(data.seq, pinfo);

      if (data.vport < 0)
      {
        error<driver_error>("midi_in_alsa::open_port: ALSA error creating input port.");
        return false;
      }
      data.vport = snd_seq_port_info_get_port(pinfo);
    }
    return true;
  }

  [[nodiscard]] bool start_thread()
  {
// Start the input queue
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
    snd_seq_start_queue(data.seq, data.queue_id, nullptr);
    snd_seq_drain_output(data.seq);
#endif
    // Start our MIDI input thread.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);

    data.doInput = true;
    int err = pthread_create(&data.thread, &attr, alsaMidiHandler, &inputData_);
    pthread_attr_destroy(&attr);
    if (err)
    {
      if (data.subscription)
      {
        snd_seq_unsubscribe_port(data.seq, data.subscription);
        snd_seq_port_subscribe_free(data.subscription);
        data.subscription = nullptr;
      }
      data.doInput = false;
      error<thread_error>("midi_in_alsa::start_thread: error starting MIDI input thread!");
      return false;
    }
    return true;
  }

  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (connected_)
    {
      warning("midi_in_alsa::open_port: a valid connection already exists!");
      return;
    }

    // Find the port to which we want to connect to
    unsigned int nSrc = this->get_port_count();
    if (nSrc < 1)
    {
      error<no_devices_found_error>("midi_in_alsa::open_port: no MIDI input sources found!");
      return;
    }

    snd_seq_port_info_t* src_pinfo{};
    snd_seq_port_info_alloca(&src_pinfo);
    if (alsa_seq::port_info(
            data.seq, src_pinfo, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            (int)portNumber)
        == 0)
    {
      error<invalid_parameter_error>(
          "midi_in_alsa::open_port: invalid 'portNumber' argument: " + std::to_string(portNumber));
      return;
    }

    snd_seq_addr_t sender{}, receiver{};
    sender.client = snd_seq_port_info_get_client(src_pinfo);
    sender.port = snd_seq_port_info_get_port(src_pinfo);
    receiver.client = snd_seq_client_id(data.seq);

    if (!create_port(portName))
      return;

    receiver.port = data.vport;

    // Create the connection between ports
    if (!data.subscription)
    {
      // Make subscription
      if (snd_seq_port_subscribe_malloc(&data.subscription) < 0)
      {
        error<driver_error>("midi_in_alsa::open_port: ALSA error allocation port subscription.");
        return;
      }
      snd_seq_port_subscribe_set_sender(data.subscription, &sender);
      snd_seq_port_subscribe_set_dest(data.subscription, &receiver);
      if (snd_seq_subscribe_port(data.seq, data.subscription))
      {
        snd_seq_port_subscribe_free(data.subscription);
        data.subscription = nullptr;
        error<driver_error>("midi_in_alsa::open_port: ALSA error making port connection.");
        return;
      }
    }

    // Run
    if (data.doInput == false)
    {
      if (!start_thread())
        return;
    }
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (!create_port(portName))
      return;

    if (data.doInput == false)
    {
      // Wait for old thread to stop, if still running
      if (!pthread_equal(data.thread, data.dummy_thread_id))
        pthread_join(data.thread, nullptr);

      if (!start_thread())
        return;
    }
    connected_ = true;
  }

  void close_port() override
  {
    if (connected_)
    {
      if (data.subscription)
      {
        snd_seq_unsubscribe_port(data.seq, data.subscription);
        snd_seq_port_subscribe_free(data.subscription);
        data.subscription = nullptr;
      }
      // Stop the input queue
#ifndef LIBREMIDI_ALSA_AVOID_TIMESTAMPING
      snd_seq_stop_queue(data.seq, data.queue_id, nullptr);
      snd_seq_drain_output(data.seq);
#endif
      connected_ = false;
    }

    // Stop thread to avoid triggering the callback, while the port is intended
    // to be closed
    if (data.doInput)
    {
      data.doInput = false;
      write(data.trigger_fds[1], &data.doInput, sizeof(data.doInput));

      if (!pthread_equal(data.thread, data.dummy_thread_id))
        pthread_join(data.thread, nullptr);
    }
  }

  void set_client_name(std::string_view clientName) override { data.set_client_name(clientName); }

  void set_port_name(std::string_view portName) override { data.set_port_name(portName); }

  unsigned int get_port_count() const override
  {
    return data.get_port_count(SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ);
  }

  std::string get_port_name(unsigned int portNumber) const override
  {
    return data.get_port_name(portNumber, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ);
  }

private:
  static void* alsaMidiHandler(void* ptr)
  {
    auto& data = *static_cast<midi_in_api::in_data*>(ptr);
    auto& apidata = *static_cast<alsa_in_data*>(data.apiData);

    double time{};
    bool continueSysex = false;
    bool doDecode = false;
    message& message = data.message;
    int poll_fd_count{};
    pollfd* poll_fds{};

    snd_seq_event_t* ev{};

    apidata.bufferSize = 32;
    int result = snd_midi_event_new(0, &apidata.coder);
    if (result < 0)
    {
      apidata.doInput = false;
#if defined(__LIBREMIDI_DEBUG__)
      std::cerr << "\nmidi_in_alsa::alsaMidiHandler: error initializing MIDI "
                   "event parser!\n\n";
#endif
      return nullptr;
    }

    std::vector<unsigned char> buffer;
    buffer.resize(apidata.bufferSize);

    snd_midi_event_init(apidata.coder);
    snd_midi_event_no_status(apidata.coder, 1); // suppress running status messages

    poll_fd_count = snd_seq_poll_descriptors_count(apidata.seq, POLLIN) + 1;
    poll_fds = (struct pollfd*)alloca(poll_fd_count * sizeof(struct pollfd));
    snd_seq_poll_descriptors(apidata.seq, poll_fds + 1, poll_fd_count - 1, POLLIN);
    poll_fds[0].fd = apidata.trigger_fds[0];
    poll_fds[0].events = POLLIN;

    while (apidata.doInput)
    {
      if (snd_seq_event_input_pending(apidata.seq, 1) == 0)
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

      // If here, there should be data.
      result = snd_seq_event_input(apidata.seq, &ev);
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
          if (!(data.ignoreFlags & 0x02))
            doDecode = true;
          break;

        case SND_SEQ_EVENT_TICK: // 0xF9 ... MIDI timing tick
          if (!(data.ignoreFlags & 0x02))
            doDecode = true;
          break;

        case SND_SEQ_EVENT_CLOCK: // 0xF8 ... MIDI timing (clock) tick
          if (!(data.ignoreFlags & 0x02))
            doDecode = true;
          break;

        case SND_SEQ_EVENT_SENSING: // Active sensing
          if (!(data.ignoreFlags & 0x04))
            doDecode = true;
          break;

        case SND_SEQ_EVENT_SYSEX: {
          if ((data.ignoreFlags & 0x01))
            break;
          if (ev->data.ext.len > apidata.bufferSize)
          {
            apidata.bufferSize = ev->data.ext.len;
            buffer.resize(apidata.bufferSize);
          }
          doDecode = true;
          break;
        }

        default:
          doDecode = true;
      }

      if (doDecode)
      {
        uint64_t nBytes
            = snd_midi_event_decode(apidata.coder, buffer.data(), apidata.bufferSize, ev);
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

            // Calculate the time stamp:
            message.timestamp = 0;

            // Method 1: Use the system time.
            // gettimeofday(&tv, (struct timezone *)nullptr);
            // time = (tv.tv_sec * 1000000) + tv.tv_usec;

            // Method 2: Use the ALSA sequencer event time data.
            // (thanks to Pedro Lopez-Cabanillas!).

            // Using method from:
            // https://www.gnu.org/software/libc/manual/html_node/Elapsed-Time.html

            // Perform the carry for the later subtraction by updating y.
            snd_seq_real_time_t& x(ev->time.time);
            snd_seq_real_time_t& y(apidata.lastTime);
            if (x.tv_nsec < y.tv_nsec)
            {
              int nsec = (y.tv_nsec - x.tv_nsec) / 1000000000 + 1;
              y.tv_nsec -= 1000000000 * nsec;
              y.tv_sec += nsec;
            }
            if (x.tv_nsec - y.tv_nsec > 1000000000)
            {
              int nsec = (x.tv_nsec - y.tv_nsec) / 1000000000;
              y.tv_nsec += 1000000000 * nsec;
              y.tv_sec -= nsec;
            }

            // Compute the time difference.
            time = x.tv_sec - y.tv_sec + (x.tv_nsec - y.tv_nsec) * 1e-9;

            apidata.lastTime = ev->time.time;

            if (data.firstMessage == true)
            {
              data.firstMessage = false;
              message.timestamp = 0;
            }
            else
            {
              message.timestamp = time;
            }
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

      data.on_message_received(std::move(message));
    }

    snd_midi_event_free(apidata.coder);
    apidata.coder = nullptr;
    apidata.thread = apidata.dummy_thread_id;
    return nullptr;
  }

  alsa_in_data data;
};

}
