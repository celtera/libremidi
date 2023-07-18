#pragma once
#include <libremidi/backends/alsa_raw/config.hpp>
#include <libremidi/backends/alsa_raw/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>

#include <alsa/asoundlib.h>

namespace libremidi
{
class midi_in_raw_alsa final : public midi_in_default<midi_in_raw_alsa>
{
public:
  static const constexpr auto backend = "Raw ALSA";

  explicit midi_in_raw_alsa(std::string_view clientName)
      : midi_in_default<midi_in_raw_alsa>{nullptr}
  {
  }

  ~midi_in_raw_alsa() override
  {
    // Close a connection if it exists.
    midi_in_raw_alsa::close_port();
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::LINUX_ALSA_RAW;
  }

  void open_port(unsigned int portNumber, std::string_view) override
  {
    if (connected_)
    {
      warning("midi_in_raw_alsa::open_port: a valid connection already exists.");
      return;
    }

    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    unsigned int num = device_list.inputs.size();
    if (portNumber >= num)
    {
      error<no_devices_found_error>("midi_in_raw_alsa::open_port: no MIDI output sources found.");
      return;
    }

    const int mode = SND_RAWMIDI_NONBLOCK;
    const char* portname = device_list.inputs[portNumber].device.c_str();
    int status = snd_rawmidi_open(&midiport_, nullptr, portname, mode);
    if (status < 0)
    {
      error<driver_error>("midi_in_raw_alsa::open_port: cannot open device.");
      return;
    }

    this->thread_ = std::thread{[this] {
      running_ = true;
      run_thread();
    }};

    connected_ = true;
  }

  void init_pollfd()
  {
    int num_fds = snd_rawmidi_poll_descriptors_count(this->midiport_);

    this->fds_.clear();
    this->fds_.resize(num_fds);

    snd_rawmidi_poll_descriptors(this->midiport_, fds_.data(), num_fds);
  }

  void run_thread()
  {
    static const constexpr int poll_timeout = 50; // in ms

    init_pollfd();

    while (this->running_)
    {
      // Poll
      int err = poll(fds_.data(), fds_.size(), poll_timeout);
      if (err < 0)
        return;

      if (!this->running_)
        return;

      // Read events
      unsigned short res{};
      err = snd_rawmidi_poll_descriptors_revents(this->midiport_, fds_.data(), fds_.size(), &res);
      if (err < 0)
        return;

      // Did we encounter an error during polling
      if (res & (POLLERR | POLLHUP))
        return;

      // Is there data to read
      if (res & POLLIN)
      {
        if (!read_input_buffer())
          return;
      }
    }
  }

  bool read_input_buffer()
  {
    static const constexpr int nbytes = 1024;

    unsigned char bytes[nbytes];
    const int err = snd_rawmidi_read(this->midiport_, bytes, nbytes);
    if (err > 0)
    {
      // err is the amount of bytes read in that case
      const int length = filter_input_buffer(bytes, err);
      if (length == 0)
        return true;

      // we have "length" midi bytes ready to be processed.
      decoder_.add_bytes(bytes, length);
      return true;
    }
    else if (err < 0 && err != -EAGAIN)
    {
      return false;
    }

    return true;
  }

  int filter_input_buffer(unsigned char* data, int size)
  {
    if (!filter_active_sensing_)
      return size;

    return std::remove(data, data + size, 0xFE) - data;
  }

  void close_port() override
  {
    if (connected_)
    {
      running_ = false;
      thread_.join();

      snd_rawmidi_close(midiport_);
      midiport_ = nullptr;
      connected_ = false;
    }
  }

  unsigned int get_port_count() override
  {
    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    return device_list.inputs.size();
  }

  std::string get_port_name(unsigned int portNumber) override
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
        = [this](std::string_view text) { this->error<driver_error>(text); };
    return device_list;
  }

  snd_rawmidi_t* midiport_{};
  std::thread thread_;
  std::atomic_bool running_{};
  std::vector<pollfd> fds_;
  midi_stream_decoder decoder_{this->inputData_};

  bool filter_active_sensing_ = false;
};
}
