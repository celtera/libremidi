#pragma once
#include <libremidi/backends/alsa_raw/config.hpp>
#include <libremidi/backends/alsa_raw/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>

#include <alsa/asoundlib.h>

#include <atomic>
#include <thread>

namespace libremidi
{
class midi_in_raw_alsa
    : public midi_in_api
    , public error_handler
{
public:
  struct
      : input_configuration
      , alsa_raw_input_configuration
  {
  } configuration;

  explicit midi_in_raw_alsa(input_configuration&& conf, alsa_raw_input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  }

  ~midi_in_raw_alsa() override
  {
    // Close a connection if it exists.
    midi_in_raw_alsa::close_port();
  }

  void open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_in_raw_alsa: open_virtual_port unsupported");
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_raw_alsa: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_in_raw_alsa: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::LINUX_ALSA_RAW;
  }

  void init_port(unsigned int portNumber)
  {
    if (connected_)
    {
      warning(
          this->configuration, "midi_in_raw_alsa::open_port: a valid connection already exists.");
      return;
    }

    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    unsigned int num = device_list.inputs.size();
    if (portNumber >= num)
    {
      error<no_devices_found_error>(
          this->configuration, "midi_in_raw_alsa::open_port: no MIDI output sources found.");
      return;
    }

    constexpr int mode = SND_RAWMIDI_NONBLOCK;
    const char* portname = device_list.inputs[portNumber].device.c_str();
    int status = snd_rawmidi_open(&midiport_, nullptr, portname, mode);
    if (status < 0)
    {
      error<driver_error>(this->configuration, "midi_in_raw_alsa::open_port: cannot open device.");
      return;
    }

    snd_rawmidi_params_t* params{};
    snd_rawmidi_params_alloca(&params);
    int err = snd_rawmidi_params_current(midiport_, params);
    err = snd_rawmidi_params_set_no_active_sensing(midiport_, params, 1);

    if (configuration.timestamps == input_configuration::NoTimestamp)
    {
      err = snd_rawmidi_params_set_read_mode(midiport_, params, SND_RAWMIDI_READ_STANDARD);
      err = snd_rawmidi_params_set_clock_type(midiport_, params, SND_RAWMIDI_CLOCK_NONE);
    }
    else
    {
      err = snd_rawmidi_params_set_read_mode(midiport_, params, SND_RAWMIDI_READ_TSTAMP);
      err = snd_rawmidi_params_set_clock_type(midiport_, params, SND_RAWMIDI_CLOCK_MONOTONIC);
    }

    err = snd_rawmidi_params(midiport_, params);

    init_pollfd();
  }

  void init_pollfd()
  {
    int num_fds = snd_rawmidi_poll_descriptors_count(this->midiport_);

    this->fds_.clear();
    this->fds_.resize(num_fds);

    snd_rawmidi_poll_descriptors(this->midiport_, fds_.data(), num_fds);
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
          = snd_rawmidi_poll_descriptors_revents(this->midiport_, fds.data(), fds.size(), &res);
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

    int z = 0;
    int err = 0;
    while ((err = snd_rawmidi_read(this->midiport_, bytes, nbytes)) > 0)
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
      case input_configuration::NoTimestamp:
        break;
      case input_configuration::Relative: {
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
      case input_configuration::Absolute:
      case input_configuration::SystemMonotonic:
        res = int64_t(ts.tv_sec) * nanos + int64_t(ts.tv_nsec);
        break;
    }
  }

  int read_input_buffer_with_timestamps()
  {
    static const constexpr int nbytes = 1024;

    unsigned char bytes[nbytes];
    struct timespec ts;

    int err = 0;
    while ((err = snd_rawmidi_tread(this->midiport_, &ts, bytes, nbytes)) > 0)
    {
      // err is the amount of bytes read
      int64_t ns{};
      set_timestamp(ts, ns);
      decoder_.add_bytes(bytes, err, ns);
    }
    return err;
  }

  void close_port() override
  {
    if (connected_)
    {
      snd_rawmidi_close(midiport_);
      midiport_ = nullptr;
      connected_ = false;
    }
  }

  unsigned int get_port_count() const override
  {
    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    return device_list.inputs.size();
  }

  std::string get_port_name(unsigned int portNumber) const override
  {
    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    if (portNumber < device_list.inputs.size())
    {
      return device_list.inputs[portNumber].pretty_name();
    }

    return {};
  }

  raw_alsa_helpers::enumerator get_device_enumerator() const noexcept
  {
    raw_alsa_helpers::enumerator device_list;
    device_list.error_callback
        = [this](std::string_view text) { this->error<driver_error>(this->configuration, text); };
    return device_list;
  }

  snd_rawmidi_t* midiport_{};
  std::vector<pollfd> fds_;
  midi_stream_decoder decoder_{this->configuration.on_message};
  int64_t last_time{};
};

class midi_in_raw_alsa_threaded : public midi_in_raw_alsa
{
  using midi_in_raw_alsa::midi_in_raw_alsa;

  void run_thread(auto parse_func)
  {
    static const constexpr int poll_timeout = 50; // in ms

    while (this->running_)
    {
      // Poll
      int err = poll(fds_.data(), fds_.size(), poll_timeout);
      if (err == -EAGAIN)
        continue;
      else if (err < 0)
        return;

      if (!this->running_)
        return;

      err = do_read_events(parse_func, {fds_.data(), fds_.size()});
      if (err == -EAGAIN)
        continue;
      else if (err < 0)
        return;
    }
  }

  void open_port(unsigned int portNumber, std::string_view name) override
  {
    midi_in_raw_alsa::init_port(portNumber);

    if (configuration.timestamps == input_configuration::NoTimestamp)
    {
      this->thread_ = std::thread{[this] {
        running_ = true;
        run_thread(&midi_in_raw_alsa::read_input_buffer);
      }};
    }
    else
    {
      this->thread_ = std::thread{[this] {
        running_ = true;
        run_thread(&midi_in_raw_alsa::read_input_buffer_with_timestamps);
      }};
    }

    connected_ = true;
  }

  void close_port() override
  {
    if (running_)
    {
      running_ = false;
      if (thread_.joinable())
        thread_.join();
    }

    midi_in_raw_alsa::close_port();
  }

  std::thread thread_;
  std::atomic_bool running_{};
};

class midi_in_raw_alsa_manual : public midi_in_raw_alsa
{
  using midi_in_raw_alsa::midi_in_raw_alsa;

  void open_port(unsigned int portNumber, std::string_view name) override
  {
    midi_in_raw_alsa::init_port(portNumber);

    if (configuration.timestamps == input_configuration::NoTimestamp)
    {
      configuration.manual_poll(manual_poll_parameters{
          .fds = {this->fds_.data(), this->fds_.size()},
          .callback = [this](std::span<pollfd> fds) {
            return do_read_events(&midi_in_raw_alsa::read_input_buffer, fds);
          }});
    }
    else
    {
      configuration.manual_poll(manual_poll_parameters{
          .fds = {this->fds_.data(), this->fds_.size()},
          .callback = [this](std::span<pollfd> fds) {
            return do_read_events(&midi_in_raw_alsa::read_input_buffer_with_timestamps, fds);
          }});
    }

    connected_ = true;
  }
};

template <>
inline std::unique_ptr<midi_in_api> make<midi_in_raw_alsa>(
    libremidi::input_configuration&& conf, libremidi::alsa_raw_input_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<midi_in_raw_alsa_manual>(std::move(conf), std::move(api));
  else
    return std::make_unique<midi_in_raw_alsa_threaded>(std::move(conf), std::move(api));
}
}
