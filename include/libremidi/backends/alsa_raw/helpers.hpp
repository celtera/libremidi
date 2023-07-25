#pragma once
#include <libremidi/config.hpp>
#include <libremidi/detail/observer.hpp>

#include <alsa/asoundlib.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Credits: greatly inspired from
// https://ccrma.stanford.edu/~craig/articles/linuxmidi/alsa-1.0/alsarawmidiout.c
// https://ccrma.stanford.edu/~craig/articles/linuxmidi/alsa-1.0/alsarawportlist.c
// Thanks Craig Stuart Sapp <craig@ccrma.stanford.edu>

namespace libremidi
{
namespace
{

struct alsa_raw_port_id
{
  int card{}, dev{}, port{};
  std::string to_string() const noexcept
  {
    return "hw:" + std::to_string(card) + "," + std::to_string(dev) + "," + std::to_string(port);
  }
};
inline constexpr port_handle raw_to_port_handle(alsa_raw_port_id id) noexcept
{
  return (uint64_t(id.card) << 32) + (uint64_t(id.dev) << 16) + id.port;
}
inline constexpr alsa_raw_port_id raw_from_port_handle(port_handle p) noexcept
{
  alsa_raw_port_id ret;
  ret.card = (p & 0x00'00'FF'FF'00'00'00'00) >> 32;
  ret.dev = (p & 0x00'00'00'00'FF'FF'00'00) >> 16;
  ret.port = (p & 0x00'00'00'00'00'00'FF'FF);
  return ret;
}
static_assert(raw_from_port_handle(raw_to_port_handle({102, 7, 3})).card == 102);
static_assert(raw_from_port_handle(raw_to_port_handle({12, 7, 3})).dev == 7);
static_assert(raw_from_port_handle(raw_to_port_handle({12, 7, 3})).port == 3);

struct raw_alsa_helpers
{
  struct alsa_raw_port_info
  {
    std::string device;
    std::string card_name;
    std::string device_name;
    std::string subdevice_name;
    int card{}, dev{}, sub{};

    std::string pretty_name() const
    {
      return device + ": " + card_name + " : " + device_name + " : " + subdevice_name;
    }

    bool operator==(const alsa_raw_port_info& other) const noexcept = default;
  };

  struct enumerator;
  struct snd_ctl_wrapper
  {
    snd_ctl_t* ctl{};
    snd_ctl_wrapper(enumerator& self, const char* name)
    {
      int status = snd_ctl_open(&ctl, name, 0);
      if (status < 0)
      {
        self.error(
            "raw_alsa_helpers::enumerator::snd_ctl_wrapper: "
            "cannot open control for card",
            name, snd_strerror(status));
      }
    }

    ~snd_ctl_wrapper()
    {
      if (ctl)
      {
        snd_ctl_close(ctl);
      }
    }

    snd_ctl_t& operator*() const noexcept { return *ctl; }
    snd_ctl_t* operator->() const noexcept { return ctl; }
    operator snd_ctl_t*() const noexcept { return ctl; }
  };

  struct enumerator
  {
    std::vector<alsa_raw_port_info> inputs;
    std::vector<alsa_raw_port_info> outputs;

    std::function<void(std::string_view)> error_callback;
    std::function<void(std::string_view)> warn_callback;

    template <typename... Args>
    void warning(Args&&... args)
    {
      std::string s;
      ((s += args), ...);
      if (warn_callback)
      {
        warn_callback(std::move(s));
      }
      else
      {
        std::cerr << s << std::endl;
      }
    }
    template <typename... Args>
    void error(Args&&... args)
    {
      std::string s;
      ((s += args), ...);
      if (error_callback)
      {
        error_callback(std::move(s));
      }
      else
      {
        throw std::runtime_error(s.c_str());
      }
    }

    // 1: is an input / output
    // 0: isn't an input / output
    // < 0: error
    int is(snd_rawmidi_stream_t stream, snd_ctl_t* ctl, int card, int device, int sub)
    {
      snd_rawmidi_info_t* info;

      snd_rawmidi_info_alloca(&info);
      snd_rawmidi_info_set_device(info, device);
      snd_rawmidi_info_set_subdevice(info, sub);
      snd_rawmidi_info_set_stream(info, stream);

      const int status = snd_ctl_rawmidi_info(ctl, info);
      if (status == 0)
      {
        return 1;
      }
      else if (status < 0 && status != -ENXIO)
      {
        error(
            "raw_alsa_helpers::enumerator::is: cannot get rawmidi information:", card, device, sub,
            snd_strerror(status));
        return status;
      }
      else
      {
        return 0;
      }
    }

    int is_input(snd_ctl_t* ctl, int card, int device, int sub)
    {
      return is(SND_RAWMIDI_STREAM_INPUT, ctl, card, device, sub);
    }

    int is_output(snd_ctl_t* ctl, int card, int device, int sub)
    {
      return is(SND_RAWMIDI_STREAM_OUTPUT, ctl, card, device, sub);
    }

    static std::string device_identifier(int card, int device, int sub)
    {
      std::string s;
      s.reserve(12);
      s += "hw:";
      s += std::to_string(card);
      s += ",";
      s += std::to_string(device);
      s += ",";
      s += std::to_string(sub);
      return s;
    }

    void enumerate_cards()
    {
      int card = -1;

      int status = snd_card_next(&card);
      if (status < 0)
      {
        error(
            "raw_alsa_helpers::enumerator::enumerate_cards: "
            "cannot determine card number: ",
            snd_strerror(status));
        return;
      }

      if (card < 0)
      {
        error(
            "raw_alsa_helpers::enumerator::enumerate_cards: "
            "no sound cards found");
        return;
      }

      while (card >= 0)
      {
        enumerate_devices(card);

        if ((status = snd_card_next(&card)) < 0)
        {
          error(
              "raw_alsa_helpers::enumerator::enumerate_cards: "
              "cannot determine card number: ",
              snd_strerror(status));
          break;
        }
      }
    }

    void enumerate_devices(int card)
    {
      char name[128];

      sprintf(name, "hw:%d", card);

      // Open card.
      snd_ctl_wrapper ctl{*this, name};
      if (!ctl)
        return;

      // Enumerate devices.
      int device = -1;
      do
      {
        const int status = snd_ctl_rawmidi_next_device(ctl, &device);
        if (device == -1)
          return;

        if (status < 0)
        {
          error(
              "raw_alsa_helpers::enumerator::enumerate_devices: "
              "cannot determine device number: ",
              snd_strerror(status));
          break;
        }

        if (device >= 0)
          enumerate_subdevices(ctl, card, device);

      } while (device >= 0);
    }

    static std::string get_card_name(int card)
    {
      char* card_name{};
      snd_card_get_name(card, &card_name);

      std::string str = card_name;
      free(card_name);
      return str;
    }

    void enumerate_subdevices(snd_ctl_t* ctl, int card, int device)
    {
      snd_rawmidi_info_t* info;
      snd_rawmidi_info_alloca(&info);
      snd_rawmidi_info_set_device(info, device);

      snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
      snd_ctl_rawmidi_info(ctl, info);
      const int subs_in = snd_rawmidi_info_get_subdevices_count(info);

      snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
      snd_ctl_rawmidi_info(ctl, info);
      const int subs_out = snd_rawmidi_info_get_subdevices_count(info);

      alsa_raw_port_info d;
      d.card = card;
      d.dev = device;
      d.card_name = get_card_name(card);
      d.device_name = snd_rawmidi_info_get_name(info);

      auto read_subdevice_info = [&](int sub) {
        snd_rawmidi_info_set_subdevice(info, sub);
        snd_ctl_rawmidi_info(ctl, info);

        d.device = device_identifier(card, device, sub);
        d.subdevice_name = snd_rawmidi_info_get_subdevice_name(info);
        d.sub = sub;
      };

      if (subs_in > 0)
      {
        snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
        for (int sub = 0; sub < subs_in; sub++)
        {
          read_subdevice_info(sub);
          inputs.push_back(d);
        }
      }

      if (subs_out > 0)
      {
        snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
        for (int sub = 0; sub < subs_out; sub++)
        {
          read_subdevice_info(sub);
          outputs.push_back(d);
        }
      }
    }
  };
};
}
}
