#pragma once
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/libremidi.hpp>

#include <alsa/asoundlib.h>
#include <sys/time.h>

#include <pthread.h>

#include <atomic>
#include <map>
#include <thread>

namespace libremidi
{

// Possible parameters:
// - Direct / non-direct output
// - Timer source used (high resolution, etc)
// - Timestamping
// - Tempo, ppq?

struct alsa_configuration
{
  std::string_view client_name;
};

// A structure to hold variables related to the ALSA API
// implementation.
struct alsa_data
{
  snd_seq_t* seq{};
  int vport{};

  snd_seq_port_subscribe_t* subscription{};
  snd_midi_event_t* coder{};

  void set_client_name(std::string_view clientName)
  {
    snd_seq_set_client_name(seq, clientName.data());
  }

  void set_port_name(std::string_view portName)
  {
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_get_port_info(seq, vport, pinfo);
    snd_seq_port_info_set_name(pinfo, portName.data());
    snd_seq_set_port_info(seq, vport, pinfo);
  }

  unsigned int get_port_count(int caps) const
  {
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);

    return alsa_seq::port_info(seq, pinfo, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, -1);
  }

  std::string get_port_name(unsigned int portNumber, int caps) const
  {
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);

    if (alsa_seq::port_info(
            seq, pinfo, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, (int)portNumber))
    {
      return alsa_seq::port_name(seq, pinfo);
    }

    // If we get here, we didn't find a match.
    // warning("midi_in_alsa::get_port_name: error looking for port name!");
    return {};
  }
};

}
