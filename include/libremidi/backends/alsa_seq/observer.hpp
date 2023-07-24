#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/detail/observer.hpp>

#include <alsa/asoundlib.h>

namespace libremidi
{
class observer_alsa final : public observer_api
{
public:
  explicit observer_alsa(observer::callbacks&& c)
      : observer_api{std::move(c)}
  {
    using namespace std::literals;
    int err = snd_seq_open(&seq_, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_open failed");
    }

    client_ = snd_seq_client_id(seq_);

    auto N = snd_seq_poll_descriptors_count(seq_, POLLIN);
    descriptors_.resize(N);
    snd_seq_poll_descriptors(seq_, descriptors_.data(), N, POLLIN);

    err = snd_seq_set_client_name(seq_, "libremidi-observe");
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_set_client_name failed");
    }

    err = snd_seq_create_simple_port(
        seq_, "libremidi-observe",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ
            | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_create_simple_port failed");
    }
    port_ = err;

    err = snd_seq_connect_from(seq_, port_, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_connect_from failed");
    }

    running = true;
    poll_ = std::thread{[this] {
      while (this->running)
      {
        int err = poll(descriptors_.data(), descriptors_.size(), -1);
        if (err > 0)
        {
          snd_seq_event_t* ev;
          while (snd_seq_event_input(seq_, &ev) >= 0)
          {
            handle_event(ev);
          }
        }
      }
    }};
  }

  struct port_info
  {
    std::string name;
    int client{};
    int port{};
    bool isInput{};
    bool isOutput{};
  };

  port_info get_info(int client, int port)
  {
    port_info p;
    p.client = client;
    p.port = port;

    snd_seq_client_info_t* cinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_get_any_client_info(seq_, client, cinfo);

    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_get_any_port_info(seq_, client, port, pinfo);

    p.name = std::to_string(p.client) + ":" + std::to_string(p.port);

    if (auto name = snd_seq_client_info_get_name(cinfo))
      p.name += std::string(" ") + name;
    p.name += " - ";
    if (auto name = snd_seq_port_info_get_name(pinfo))
      p.name += std::string(" ") + name;

    auto cap = snd_seq_port_info_get_capability(pinfo);
    p.isInput = (cap & SND_SEQ_PORT_CAP_DUPLEX) | (cap & SND_SEQ_PORT_CAP_READ);
    p.isOutput = (cap & SND_SEQ_PORT_CAP_DUPLEX) | (cap & SND_SEQ_PORT_CAP_WRITE);

    return p;
  }

  void handle_event(snd_seq_event_t* ev)
  {
    switch (ev->type)
    {
      case SND_SEQ_EVENT_PORT_START: {
        auto p = get_info(ev->data.addr.client, ev->data.addr.port);
        if (p.client == client_)
          return;

        knownClients_[{p.client, p.port}] = p;
        if (p.isInput && callbacks_.input_added)
        {
          callbacks_.input_added(p.port, p.name);
        }

        if (p.isOutput && callbacks_.output_added)
        {
          callbacks_.output_added(p.port, p.name);
        }
        break;
      }
      case SND_SEQ_EVENT_PORT_EXIT: {
        auto p = get_info(ev->data.addr.client, ev->data.addr.port);
        if (p.client == client_)
          return;

        auto it = knownClients_.find({p.client, p.port});
        if (it != knownClients_.end())
        {
          p = it->second;
          knownClients_.erase(it);
        }

        if (p.isInput && callbacks_.input_removed)
        {
          callbacks_.input_removed(p.port, p.name);
        }

        if (p.isOutput && callbacks_.output_added)
        {
          callbacks_.output_removed(p.port, p.name);
        }
        break;
      }
      case SND_SEQ_EVENT_PORT_CHANGE: {
        break;
      }
      default:
        break;
    }
  }

  ~observer_alsa()
  {
    running = false;
    assert(poll_.joinable());
    poll_.join();

    snd_seq_delete_port(seq_, port_);
    snd_seq_close(seq_);
  }

private:
  snd_seq_t* seq_{};
  std::atomic_bool running{false};
  std::thread poll_;
  std::vector<pollfd> descriptors_;
  std::map<std::pair<int, int>, port_info> knownClients_;
  int client_{};
  int port_{};
};
}
