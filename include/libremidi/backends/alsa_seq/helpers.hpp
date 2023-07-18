#pragma once
#include <libremidi/libremidi.hpp>

#include <alsa/asoundlib.h>
#include <string>

#include <alsa/asoundlib.h>
#include <sys/time.h>

#include <pthread.h>

#include <atomic>
#include <map>
#include <thread>

namespace libremidi
{
namespace alsa_seq
{
namespace
{
// This function is used to count or get the pinfo structure for a given port
// number.
inline unsigned int
port_info(snd_seq_t* seq, snd_seq_port_info_t* pinfo, unsigned int type, int portNumber)
{
  snd_seq_client_info_t* cinfo{};
  int count = 0;
  snd_seq_client_info_alloca(&cinfo);

  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(seq, cinfo) >= 0)
  {
    int client = snd_seq_client_info_get_client(cinfo);
    if (client == 0)
      continue;

    // Reset query info
    snd_seq_port_info_set_client(pinfo, client);
    snd_seq_port_info_set_port(pinfo, -1);
    while (snd_seq_query_next_port(seq, pinfo) >= 0)
    {
      unsigned int atyp = snd_seq_port_info_get_type(pinfo);
      if (((atyp & SND_SEQ_PORT_TYPE_MIDI_GENERIC) == 0) && ((atyp & SND_SEQ_PORT_TYPE_SYNTH) == 0)
          && ((atyp & SND_SEQ_PORT_TYPE_APPLICATION) == 0))
        continue;

      unsigned int caps = snd_seq_port_info_get_capability(pinfo);
      if ((caps & type) != type)
        continue;
      if (count == portNumber)
        return 1;
      ++count;
    }
  }

  // If a negative portNumber was used, return the port count.
  if (portNumber < 0)
    return count;
  return 0;
}

inline std::string port_name(snd_seq_t* seq, snd_seq_port_info_t* pinfo)
{
  snd_seq_client_info_t* cinfo;
  snd_seq_client_info_alloca(&cinfo);

  int cnum = snd_seq_port_info_get_client(pinfo);
  snd_seq_get_any_client_info(seq, cnum, cinfo);

  std::string str;
  str.reserve(64);
  str += snd_seq_client_info_get_name(cinfo);
  str += ":";
  str += snd_seq_port_info_get_name(pinfo);
  str += " "; // These lines added to make sure devices are listed
  str += std::to_string(
      snd_seq_port_info_get_client(pinfo)); // with full portnames added to ensure individual
      // device names
  str += ":";
  str += std::to_string(snd_seq_port_info_get_port(pinfo));
  return str;
}
}
}

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
