#pragma once
#include <libremidi/libremidi.hpp>

#include <alsa/asoundlib.h>
#include <string>

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
}