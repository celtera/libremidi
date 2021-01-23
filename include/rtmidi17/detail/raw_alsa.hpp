#pragma once
#include <alsa/asoundlib.h>
#include <ostream>
#include <rtmidi17/detail/dummy.hpp>
#include <rtmidi17/detail/midi_api.hpp>
#include <rtmidi17/detail/raw_alsa_helpers.hpp>
#include <rtmidi17/rtmidi17.hpp>
#include <sstream>

// Credits: greatly inspired from
// https://ccrma.stanford.edu/~craig/articles/linuxmidi/alsa-1.0/alsarawmidiout.c
// https://ccrma.stanford.edu/~craig/articles/linuxmidi/alsa-1.0/alsarawportlist.c
// Thanks Craig Stuart Sapp <craig@ccrma.stanford.edu>

namespace rtmidi
{
class midi_out_raw_alsa final : public midi_out_default<midi_out_raw_alsa>
{
public:
  static const constexpr auto backend = "Raw ALSA";

  midi_out_raw_alsa(std::string_view)
  {
  }

  ~midi_out_raw_alsa() override
  {
    // Close a connection if it exists.
    midi_out_raw_alsa::close_port();
  }

  rtmidi::API get_current_api() const noexcept override
  {
    return rtmidi::API::LINUX_ALSA_RAW;
  }

  void open_port(unsigned int portNumber, std::string_view) override
  {
    if (connected_)
    {
      warning("midi_out_raw_alsa::open_port: a valid connection already exists.");
      return;
    }

    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    unsigned int num = device_list.outputs.size();
    if (portNumber >= num)
    {
      error<no_devices_found_error>("midi_out_raw_alsa::open_port: no MIDI output sources found.");
      return;
    }

    const int mode = SND_RAWMIDI_SYNC;
    const char* portname = device_list.outputs[portNumber].device.c_str();
    int status = snd_rawmidi_open(NULL, &midiport_, portname, mode);
    if (status < 0)
    {
      error<driver_error>("midi_out_raw_alsa::open_port: cannot open device.");
      return;
    }

    connected_ = true;
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

  void set_client_name(std::string_view clientName) override
  {
    warning("midi_out_raw_alsa::set_client_name: unsupported.");
  }

  void set_port_name(std::string_view portName) override
  {
    warning("midi_out_raw_alsa::set_port_name: unsupported.");
  }

  unsigned int get_port_count() override
  {
    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    return device_list.outputs.size();
  }

  std::string get_port_name(unsigned int portNumber) override
  {
    auto device_list = get_device_enumerator();
    device_list.enumerate_cards();

    if (portNumber < device_list.outputs.size())
    {
      return device_list.outputs[portNumber].pretty_name();
    }

    return {};
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    if (!midiport_)
      error<invalid_use_error>(
          "midi_out_raw_alsa::send_message: trying to send a message without an open port.");

    if (!this->chunking)
    {
      write(message, size);
    }
    else
    {
      write_chunked(message, size);
    }
  }

  bool write(const unsigned char* message, size_t size)
  {
    if (snd_rawmidi_write(midiport_, message, size) < 0)
    {
      error<driver_error>("midi_out_raw_alsa::send_message: cannot write message.");
      return false;
    }

    return true;
  }

  std::size_t get_chunk_size() const noexcept
  {
    snd_rawmidi_params_t* param;
    snd_rawmidi_params_alloca(&param);
    snd_rawmidi_params_current(midiport_, param);

    std::size_t buffer_size = snd_rawmidi_params_get_buffer_size(param);
    return std::min(buffer_size, (std::size_t)chunking->size);
  }

  std::size_t get_available_bytes_to_write() const noexcept
  {
    snd_rawmidi_status_t* st{};
    snd_rawmidi_status_alloca(&st);
    snd_rawmidi_status(midiport_, st);

    return snd_rawmidi_status_get_avail(st);
  }

  // inspired from ALSA amidi.c source code
  void write_chunked(const unsigned char* const begin, size_t size)
  {
    const unsigned char* data = begin;
    const unsigned char* end = begin + size;

    snd_rawmidi_status_t* st{};
    snd_rawmidi_status_alloca(&st);

    const std::size_t chunk_size = std::min(get_chunk_size(), size);

    // Send the first buffer
    int len = chunk_size;

    if (!write(data, len))
      return;

    data += len;

    while (data < end)
    {
      // Wait for the buffer to have some space available
      const std::size_t written_bytes = data - begin;
      std::size_t available{};
      while ((available = get_available_bytes_to_write()) < chunk_size)
      {
        if (!chunking->wait(
                std::chrono::microseconds((chunk_size - available) * 320), written_bytes))
          return;
      };

      if (!chunking->wait(chunking->interval, written_bytes))
        return;

      // Write more data
      int len = end - data;

      // Maybe until the end of the sysex
      if (auto sysex_end = (unsigned char*)memchr(data, 0xf7, len))
        len = sysex_end - data + 1;

      if (len > chunk_size)
        len = chunk_size;

      if (!write(data, len))
        return;

      data += len;
    }
  }

  raw_alsa_helpers::enumerator get_device_enumerator() const noexcept
  {
    raw_alsa_helpers::enumerator device_list;
    device_list.error_callback = [this] (std::string_view text) {
      this->error<driver_error>(text);
    };
    return device_list;
  }

  snd_rawmidi_t* midiport_{};
};

struct raw_alsa_backend
{
  using midi_in = midi_in_dummy;
  using midi_out = midi_out_raw_alsa;
  using midi_observer = observer_dummy;
  static const constexpr auto API = rtmidi::API::LINUX_ALSA_RAW;
};
}
