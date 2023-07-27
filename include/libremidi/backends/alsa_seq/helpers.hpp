#pragma once
#include <libremidi/config.hpp>
#include <libremidi/detail/observer.hpp>

#include <alsa/asoundlib.h>
#include <sys/time.h>

#include <string>

namespace libremidi
{
namespace alsa_seq
{
namespace
{
inline constexpr port_handle seq_to_port_handle(uint64_t client, uint64_t port) noexcept
{
  return (client << 32) + port;
}
inline constexpr std::pair<int, int> seq_from_port_handle(port_handle p) noexcept
{
  int client = p >> 32;
  int port = p & 0xFFFFFFFF;
  return {client, port};
}
static_assert(seq_from_port_handle(seq_to_port_handle(1234, 5432)).first == 1234);
static_assert(seq_from_port_handle(seq_to_port_handle(1234, 5432)).second == 5432);

// FIXME would be much prettier with std::generator
inline void for_all_ports(
    snd_seq_t* seq, std::function<void(snd_seq_client_info_t&, snd_seq_port_info_t&)> func)
{
  snd_seq_client_info_t* cinfo{};
  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_t* pinfo{};
  snd_seq_port_info_alloca(&pinfo);

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
      func(*cinfo, *pinfo);
    }
  }
}

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
  int vport{-1};

  snd_seq_port_subscribe_t* subscription{};
  snd_midi_event_t* coder{};

  [[nodiscard]] int init_client(auto& configuration)
  {
    // Initialize or use the snd_seq client
    if (configuration.context)
    {
      seq = configuration.context;
      return 0;
    }
    else
    {
      // Set up the ALSA sequencer client.
      int ret = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
      if (ret < 0)
        return ret;

      // Set client name.
      if (!configuration.client_name.empty())
        snd_seq_set_client_name(seq, configuration.client_name.data());

      return 0;
    }
  }

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

    return alsa_seq::port_info(seq, pinfo, caps, -1);
  }

  std::optional<snd_seq_addr_t> get_port_info(const port_information& portNumber)
  {
    auto [client, port] = alsa_seq::seq_from_port_handle(portNumber.port);
    // FIXME check that the {client, port} pair actually exists
    // snd_seq_port_info_t* src_pinfo{};
    // snd_seq_port_info_alloca(&src_pinfo);
    // snd_seq_port_info_set_client(src_pinfo, client);
    // snd_seq_port_info_set_port(src_pinfo, port);

    // {
    //   self.template error<invalid_parameter_error>(
    //       self.configuration,
    //       "alsa::get_port_info: invalid 'portNumber' argument: " + std::to_string(portNumber));
    //   return {};
    // }
    snd_seq_addr_t addr;
    addr.client = client;
    addr.port = port;
    return addr;
  }

  [[nodiscard]] int
  create_port(auto& self, std::string_view portName, int caps, std::optional<int> queue)
  {
    if (this->vport < 0)
    {
      snd_seq_port_info_t* pinfo{};
      snd_seq_port_info_alloca(&pinfo);

      snd_seq_port_info_set_name(pinfo, portName.data());
      snd_seq_port_info_set_client(pinfo, 0);
      snd_seq_port_info_set_port(pinfo, 0);
      snd_seq_port_info_set_capability(
          pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
      snd_seq_port_info_set_type(
          pinfo, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
      snd_seq_port_info_set_midi_channels(pinfo, 16);

      if (queue)
      {
        snd_seq_port_info_set_timestamping(pinfo, 1);
        snd_seq_port_info_set_timestamp_real(pinfo, 1);
        snd_seq_port_info_set_timestamp_queue(pinfo, *queue);
      }

      if (int err = snd_seq_create_port(this->seq, pinfo); err < 0)
      {
        self.template error<driver_error>(
            self.configuration, "midi_in_alsa::create_port: ALSA error creating port.");
        return err;
      }
      this->vport = snd_seq_port_info_get_port(pinfo);
      return this->vport >= 0;
    }
    return 0;
  }

  int create_connection(auto& self, snd_seq_addr_t sender, snd_seq_addr_t receiver, bool realtime)
  {
    // Create the connection between ports
    // Make subscription
    if (int err = snd_seq_port_subscribe_malloc(&this->subscription); err < 0)
    {
      self.template error<driver_error>(
          self.configuration, "create_connection: ALSA error allocation port subscription.");
      return err;
    }

    snd_seq_port_subscribe_set_sender(this->subscription, &sender);
    snd_seq_port_subscribe_set_dest(this->subscription, &receiver);

    if (realtime)
    {
      snd_seq_port_subscribe_set_time_update(this->subscription, 1);
      snd_seq_port_subscribe_set_time_real(this->subscription, 1);
    }

    if (int err = snd_seq_subscribe_port(this->seq, this->subscription); err != 0)
    {
      snd_seq_port_subscribe_free(this->subscription);
      this->subscription = nullptr;
      self.template error<driver_error>(
          self.configuration, "create_connection: ALSA error making port connection.");
      return err;
    }
    return 0;
  }

  void unsubscribe()
  {
    if (this->subscription)
    {
      snd_seq_unsubscribe_port(this->seq, this->subscription);
      snd_seq_port_subscribe_free(this->subscription);
      this->subscription = nullptr;
    }
  }
};
}
