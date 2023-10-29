#pragma once
#include <libremidi/backends/alsa_raw/config.hpp>
#include <libremidi/backends/alsa_raw/helpers.hpp>
#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>

#include <alsa/asoundlib.h>

#include <atomic>
#include <thread>

namespace libremidi::alsa_raw
{
class midi_in_impl
    : public midi1::in_api
    , public error_handler
{
public:
  struct
      : input_configuration
      , alsa_raw_input_configuration
  {
  } configuration;

  const libasound& snd = libasound::instance();

  explicit midi_in_impl(input_configuration&& conf, alsa_raw_input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    fds_.reserve(4);
  }

  ~midi_in_impl() override { }

  bool open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_in_alsa_raw: open_virtual_port unsupported");
    return false;
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_alsa_raw: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_in_alsa_raw: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_RAW; }

  // Must be a string such as: "hw:2,4,1"
  [[nodiscard]] int do_init_port(const char* portname)
  {
    constexpr int mode = SND_RAWMIDI_NONBLOCK;
    if (int err = snd.rawmidi.open(&midiport_, nullptr, portname, mode); err < 0)
    {
      error<driver_error>(this->configuration, "midi_in_alsa_raw::open_port: cannot open device.");
      return err;
    }

    snd_rawmidi_params_t* params{};
    snd_rawmidi_params_alloca(&params);

    if (int err = snd.rawmidi.params_current(midiport_, params); err < 0)
      return err;
    if (int err = snd.rawmidi.params_set_no_active_sensing(midiport_, params, 1); err < 0)
      return err;
#if LIBREMIDI_ALSA_HAS_RAWMIDI_TREAD
    if (configuration.timestamps == timestamp_mode::NoTimestamp)
    {
      if (int err = snd.rawmidi.params_set_read_mode(midiport_, params, SND_RAWMIDI_READ_STANDARD);
          err < 0)
        return err;
      if (int err = snd.rawmidi.params_set_clock_type(midiport_, params, SND_RAWMIDI_CLOCK_NONE);
          err < 0)
        return err;
    }
    else
    {
      if (int err = snd.rawmidi.params_set_read_mode(midiport_, params, SND_RAWMIDI_READ_TSTAMP);
          err < 0)
        return err;
      if (int err
          = snd.rawmidi.params_set_clock_type(midiport_, params, SND_RAWMIDI_CLOCK_MONOTONIC);
          err < 0)
        return err;
    }
#endif

    if (int err = snd.rawmidi.params(midiport_, params); err < 0)
      return err;

    return init_pollfd();
  }

  [[nodiscard]] int init_port(const port_information& p)
  {
    return do_init_port(raw_from_port_handle(p.port).to_string().c_str());
  }

  [[nodiscard]] int init_pollfd()
  {
    int num_fds = snd.rawmidi.poll_descriptors_count(this->midiport_);

    this->fds_.clear();
    this->fds_.resize(num_fds);

    return snd.rawmidi.poll_descriptors(this->midiport_, fds_.data(), num_fds);
  }

  int do_read_events(auto parse_func, std::span<pollfd> fds)
  {
    // Read events
    if (fds.empty())
    {
      return (this->*parse_func)();
    }
    else
    {
      unsigned short res{};
      int err
          = snd.rawmidi.poll_descriptors_revents(this->midiport_, fds.data(), fds.size(), &res);
      if (err < 0)
        return err;

      // Did we encounter an error during polling
      if (res & (POLLERR | POLLHUP))
        return -EIO;

      // Is there data to read
      if (res & POLLIN)
        return (this->*parse_func)();
    }

    return 0;
  }

  int read_input_buffer()
  {
    static const constexpr int nbytes = 1024;

    unsigned char bytes[nbytes];

    int err = 0;
    while ((err = snd.rawmidi.read(this->midiport_, bytes, nbytes)) > 0)
    {
      // err is the amount of bytes read
      decoder_.add_bytes(bytes, err);
    }
    return err;
  }

  void set_timestamp(const struct timespec& ts, int64_t& res)
  {
    static constexpr int64_t nanos = 1e9;
    switch (configuration.timestamps)
    {
      // Unneeded here
      case timestamp_mode::NoTimestamp:
        break;
      case timestamp_mode::Relative: {
        auto t = int64_t(ts.tv_sec) * nanos + int64_t(ts.tv_nsec);
        if (firstMessage == true)
        {
          firstMessage = false;
          res = 0;
        }
        else
        {
          res = t - last_time;
        }
        last_time = t;
        break;
      }
      case timestamp_mode::Absolute:
      case timestamp_mode::SystemMonotonic:
        res = int64_t(ts.tv_sec) * nanos + int64_t(ts.tv_nsec);
        break;
    }
  }

#if LIBREMIDI_ALSA_HAS_RAWMIDI_TREAD
  int read_input_buffer_with_timestamps()
  {
    static const constexpr int nbytes = 1024;

    unsigned char bytes[nbytes];
    struct timespec ts;

    int err = 0;
    while ((err = snd.rawmidi.tread(this->midiport_, &ts, bytes, nbytes)) > 0)
    {
      // err is the amount of bytes read
      int64_t ns{};
      set_timestamp(ts, ns);
      decoder_.add_bytes(bytes, err, ns);
    }
    return err;
  }
#else
  int read_input_buffer_with_timestamps() { return read_input_buffer(); }
#endif

  void close_port() override
  {
    if (midiport_)
      snd.rawmidi.close(midiport_);
    midiport_ = nullptr;
  }

  alsa_raw::midi1_enumerator get_device_enumerator() const noexcept
  {
    alsa_raw::midi1_enumerator device_list;
    device_list.error_callback
        = [this](std::string_view text) { this->error<driver_error>(this->configuration, text); };
    return device_list;
  }

  snd_rawmidi_t* midiport_{};
  std::vector<pollfd> fds_;
  midi_stream_decoder decoder_{this->configuration.on_message};
  int64_t last_time{};
};

class midi_in_alsa_raw_threaded : public midi_in_impl
{
public:
  midi_in_alsa_raw_threaded(input_configuration&& conf, alsa_raw_input_configuration&& apiconf)
      : midi_in_impl{std::move(conf), std::move(apiconf)}
  {
    if (this->termination_event < 0)
    {
      error<driver_error>(
          this->configuration, "midi_in_alsa::initialize: error creating eventfd.");
    }
  }

  ~midi_in_alsa_raw_threaded()
  {
    // Close a connection if it exists.
    this->close_port();
  }

private:
  void run_thread(auto parse_func)
  {
    fds_.push_back(this->termination_event);

    for (;;)
    {
      // Poll
      int err = poll(fds_.data(), fds_.size(), -1);
      if (err == -EAGAIN)
        continue;
      else if (err < 0)
        return;
      else if (termination_event.ready(fds_.back()))
        break;

      err = do_read_events(parse_func, {fds_.data(), fds_.size() - 1});
      if (err == -EAGAIN)
        continue;
      else if (err < 0)
        return;
    }
  }

  [[nodiscard]] int start_thread()
  {
    try
    {
      if (configuration.timestamps == timestamp_mode::NoTimestamp)
      {
        this->thread_ = std::thread{[this] { run_thread(&midi_in_impl::read_input_buffer); }};
      }
      else
      {
        this->thread_ = std::thread{
            [this] { run_thread(&midi_in_impl::read_input_buffer_with_timestamps); }};
      }
    }
    catch (const std::system_error& e)
    {
      using namespace std::literals;

      error<thread_error>(
          this->configuration,
          "midi_in_alsa::start_thread: error starting MIDI input thread: "s + e.what());
      return false;
    }
    return true;
  }

  bool open_port(const input_port& port, std::string_view name) override
  {
    if (int err = midi_in_impl::init_port(port); err < 0)
      return false;
    if (!start_thread())
      return false;
    return true;
  }

  void close_port() override
  {
    termination_event.notify();
    if (thread_.joinable())
      thread_.join();
    termination_event.consume(); // Reset to zero

    midi_in_impl::close_port();
  }

  std::thread thread_;
  eventfd_notifier termination_event{};
};

class midi_in_alsa_raw_manual : public midi_in_impl
{
public:
  using midi_in_impl::midi_in_impl;

  ~midi_in_alsa_raw_manual()
  {
    // Close a connection if it exists.
    this->close_port();
  }

private:
  void send_poll_callback()
  {
    if (configuration.timestamps == timestamp_mode::NoTimestamp)
    {
      configuration.manual_poll(manual_poll_parameters{
          .fds = {this->fds_.data(), this->fds_.size()},
          .callback = [this](std::span<pollfd> fds) {
            return do_read_events(&midi_in_impl::read_input_buffer, fds);
          }});
    }
    else
    {
      configuration.manual_poll(manual_poll_parameters{
          .fds = {this->fds_.data(), this->fds_.size()},
          .callback = [this](std::span<pollfd> fds) {
            return do_read_events(&midi_in_impl::read_input_buffer_with_timestamps, fds);
          }});
    }
  }

  bool open_port(const input_port& p, std::string_view name) override
  {
    if (midi_in_impl::init_port(p) < 0)
      return false;
    send_poll_callback();
    return true;
  }
};
}

namespace libremidi
{
template <>
inline std::unique_ptr<midi_in_api> make<alsa_raw::midi_in_impl>(
    libremidi::input_configuration&& conf, libremidi::alsa_raw_input_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<alsa_raw::midi_in_alsa_raw_manual>(std::move(conf), std::move(api));
  else
    return std::make_unique<alsa_raw::midi_in_alsa_raw_threaded>(std::move(conf), std::move(api));
}
}
